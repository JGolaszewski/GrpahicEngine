#include "runtime/include/PrismNdfInferenceService.h"

#include "rendering/include/renderer/PrismNdfMath.h"
#include "PrismNdfInferenceControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if ENGINE_ENABLE_PRISM_INFERENCE
#  include <nlohmann/json.hpp>
#  include <onnxruntime_cxx_api.h>

#  ifdef _WIN32
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    include <Windows.h>
#  endif
#endif

namespace Engine {

struct PrismNdfInferenceService::Impl {
  explicit Impl(PrismNdfInferenceOptions configuredOptions) : Options(std::move(configuredOptions)) {
#if ENGINE_ENABLE_PRISM_INFERENCE
    Worker = std::thread([this] { WorkerMain(); });
#endif
  }

  ~Impl() {
#if ENGINE_ENABLE_PRISM_INFERENCE
    {
      std::lock_guard lock(Mutex);
      Stopping = true;
      if (ActiveRequest != 0) {
        Cancelled.insert(ActiveRequest);
      }
    }
    Wake.notify_all();
    if (Worker.joinable()) {
      Worker.join();
    }
#endif
  }

  PrismNdfInferenceOptions Options;
  mutable std::mutex Mutex;
  std::condition_variable Wake;
  std::unordered_map<uint64_t, PrismNdfInferenceStatus> States;
  std::unordered_map<uint64_t, std::pair<std::string, bool>> Pending;
  std::unordered_set<uint64_t> Cancelled;
  std::thread Worker;
  uint64_t NextRequestId = 0;
  uint64_t ActiveRequest = 0;
  bool Stopping = false;
  bool RuntimeInitialized = false;
  std::string InitializationError;

#if ENGINE_ENABLE_PRISM_INFERENCE
  struct RuntimeModels {
    std::unique_ptr<Ort::Env> Environment;
    std::unique_ptr<Ort::Session> Encoder;
    std::unique_ptr<Ort::Session> Predictor;
    std::unique_ptr<Ort::Session> Decoder;
  };

#  ifdef _WIN32
  struct TokenizerWorker {
    HANDLE Process = nullptr;
    HANDLE ProcessThread = nullptr;
    HANDLE StdoutRead = nullptr;
    HANDLE StdinWrite = nullptr;
    HANDLE StderrRead = nullptr;
    std::thread StderrDrain;
    std::mutex DiagnosticsMutex;
    std::string Diagnostics;
    std::string StdoutBuffer;
  };
#  endif

  struct TensorContract {
    std::string_view Name;
    ONNXTensorElementDataType Type;
    std::span<const int64_t> Shape;
  };

  static void ValidateTensors(Ort::Session& session, bool inputs, std::span<const TensorContract> expected) {
    Ort::AllocatorWithDefaultOptions allocator;
    const size_t count = inputs ? session.GetInputCount() : session.GetOutputCount();
    if (count != expected.size()) {
      throw std::runtime_error("PRISM ONNX model tensor count does not match its pinned contract");
    }
    for (size_t index = 0; index < count; ++index) {
      auto allocatedName =
          inputs ? session.GetInputNameAllocated(index, allocator) : session.GetOutputNameAllocated(index, allocator);
      const std::string name(allocatedName.get());
      const auto contract =
          std::ranges::find_if(expected, [&name](const TensorContract& value) { return value.Name == name; });
      if (contract == expected.end()) {
        throw std::runtime_error("Unexpected PRISM ONNX tensor name: " + name);
      }
      auto typeInfo = inputs ? session.GetInputTypeInfo(index) : session.GetOutputTypeInfo(index);
      const auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
      const auto shape = tensorInfo.GetShape();
      if (tensorInfo.GetElementType() != contract->Type || !std::ranges::equal(shape, contract->Shape)) {
        throw std::runtime_error("PRISM ONNX tensor has the wrong type or shape: " + name);
      }
    }
  }

  void InitializeModels(RuntimeModels& models) {
    models.Environment = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "GraphicEngine-PRISM");
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(0);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    const auto encoderPath = Options.modelDirectory / "e5_encoder.onnx";
    const auto predictorPath = Options.modelDirectory / "prism_text_predictor_128.onnx";
    const auto decoderPath = Options.modelDirectory / "prism_vae_decoder_128.onnx";
    models.Encoder = std::make_unique<Ort::Session>(*models.Environment, encoderPath.c_str(), sessionOptions);
    models.Predictor = std::make_unique<Ort::Session>(*models.Environment, predictorPath.c_str(), sessionOptions);
    models.Decoder = std::make_unique<Ort::Session>(*models.Environment, decoderPath.c_str(), sessionOptions);
    const std::array<int64_t, 2> tokenShape{1, 128};
    const std::array<int64_t, 2> embeddingShape{1, 1024};
    const std::array<int64_t, 2> controlsShape{1, 7};
    const std::array<int64_t, 2> latentShape{1, 16};
    const std::array<int64_t, 2> gridShape{1, 16384};
    const std::array<TensorContract, 2> encoderInputs{{
        {"input_ids", ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, tokenShape},
        {"attention_mask", ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, tokenShape},
    }};
    const std::array<TensorContract, 1> encoderOutputs{{
        {"normalized_embedding", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, embeddingShape},
    }};
    const std::array<TensorContract, 2> predictorInputs{{
        {"embedding", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, embeddingShape},
        {"control_features", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, controlsShape},
    }};
    const std::array<TensorContract, 1> predictorOutputs{{
        {"predicted_latent", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, latentShape},
    }};
    const std::array<TensorContract, 1> decoderInputs{{
        {"latent", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, latentShape},
    }};
    const std::array<TensorContract, 1> decoderOutputs{{
        {"ndf_grid_flat", ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, gridShape},
    }};
    ValidateTensors(*models.Encoder, true, encoderInputs);
    ValidateTensors(*models.Encoder, false, encoderOutputs);
    ValidateTensors(*models.Predictor, true, predictorInputs);
    ValidateTensors(*models.Predictor, false, predictorOutputs);
    ValidateTensors(*models.Decoder, true, decoderInputs);
    ValidateTensors(*models.Decoder, false, decoderOutputs);
  }

  auto IsCancelled(uint64_t id) -> bool {
    std::lock_guard lock(Mutex);
    return Stopping || Cancelled.contains(id);
  }

  void SetStatus(uint64_t id, PrismNdfInferencePhase phase, float progress, std::string message = {},
                 std::shared_ptr<const PrismNdfData> result = {}) {
    std::lock_guard lock(Mutex);
    auto found = States.find(id);
    if (found != States.end()) {
      found->second.phase = phase;
      found->second.progress = progress;
      found->second.message = std::move(message);
      found->second.result = std::move(result);
    }
  }

#  ifdef _WIN32
  static auto Quote(const std::filesystem::path& path) -> std::wstring { return L"\"" + path.wstring() + L"\""; }

  static auto ReadLine(TokenizerWorker& worker, std::string& line) -> bool {
    constexpr size_t maximumMessageSize = size_t{1024} * 1024;
    std::array<char, 4096> buffer{};
    while (true) {
      const size_t delimiter = worker.StdoutBuffer.find('\n');
      if (delimiter != std::string::npos) {
        line.assign(worker.StdoutBuffer, 0, delimiter);
        worker.StdoutBuffer.erase(0, delimiter + 1);
        return true;
      }
      if (worker.StdoutBuffer.size() >= maximumMessageSize) {
        return false;
      }
      DWORD read = 0;
      if (!ReadFile(worker.StdoutRead, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0) {
        return false;
      }
      worker.StdoutBuffer.append(buffer.data(), static_cast<size_t>(read));
    }
    return false;
  }

  static void IncludeWorkerDiagnostics(TokenizerWorker& worker, std::string& error) {
    std::lock_guard lock(worker.DiagnosticsMutex);
    if (!worker.Diagnostics.empty()) {
      error += " (tokenizer stderr: " + worker.Diagnostics + ")";
    }
  }

  auto StartTokenizer(TokenizerWorker& worker, std::string& error) -> bool {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childStdoutWrite = nullptr;
    HANDLE childStdinRead = nullptr;
    HANDLE childStderrWrite = nullptr;
    if (!CreatePipe(&worker.StdoutRead, &childStdoutWrite, &security, 0) ||
        !CreatePipe(&childStdinRead, &worker.StdinWrite, &security, 0) ||
        !CreatePipe(&worker.StderrRead, &childStderrWrite, &security, 0)) {
      error = "Could not create PRISM tokenizer worker pipes";
      if (worker.StdoutRead != nullptr)
        CloseHandle(worker.StdoutRead);
      if (childStdoutWrite != nullptr)
        CloseHandle(childStdoutWrite);
      if (childStdinRead != nullptr)
        CloseHandle(childStdinRead);
      if (worker.StdinWrite != nullptr)
        CloseHandle(worker.StdinWrite);
      if (worker.StderrRead != nullptr)
        CloseHandle(worker.StderrRead);
      if (childStderrWrite != nullptr)
        CloseHandle(childStderrWrite);
      worker.StdoutRead = worker.StdinWrite = worker.StderrRead = nullptr;
      return false;
    }
    (void)SetHandleInformation(worker.StdoutRead, HANDLE_FLAG_INHERIT, 0);
    (void)SetHandleInformation(worker.StdinWrite, HANDLE_FLAG_INHERIT, 0);
    (void)SetHandleInformation(worker.StderrRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childStdinRead;
    startup.hStdOutput = childStdoutWrite;
    startup.hStdError = childStderrWrite;
    PROCESS_INFORMATION process{};
    std::wstring command = Quote(Options.pythonExecutable) + L" " + Quote(Options.tokenizerWorker);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const BOOL launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                         nullptr, Options.pythonExecutable.parent_path().c_str(), &startup, &process);
    const DWORD launchError = launched ? ERROR_SUCCESS : GetLastError();
    CloseHandle(childStdoutWrite);
    CloseHandle(childStdinRead);
    CloseHandle(childStderrWrite);
    if (!launched) {
      CloseHandle(worker.StdoutRead);
      CloseHandle(worker.StdinWrite);
      CloseHandle(worker.StderrRead);
      worker.StdoutRead = worker.StdinWrite = worker.StderrRead = nullptr;
      error = "Could not launch bundled PRISM tokenizer worker (Windows error " + std::to_string(launchError) + ")";
      return false;
    }
    worker.Process = process.hProcess;
    worker.ProcessThread = process.hThread;
    worker.StderrDrain = std::thread([&worker] {
      constexpr size_t maximumDiagnosticSize = size_t{64} * 1024;
      std::array<char, 2048> buffer{};
      DWORD read = 0;
      while (ReadFile(worker.StderrRead, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) &&
             read > 0) {
        std::lock_guard lock(worker.DiagnosticsMutex);
        const size_t available = maximumDiagnosticSize - (std::min)(worker.Diagnostics.size(), maximumDiagnosticSize);
        worker.Diagnostics.append(buffer.data(), (std::min)(available, static_cast<size_t>(read)));
      }
    });
    std::string line;
    if (!ReadLine(worker, line)) {
      error = "PRISM tokenizer worker exited before its ready message";
      StopTokenizer(worker, true);
      IncludeWorkerDiagnostics(worker, error);
      return false;
    }
    try {
      const auto ready = nlohmann::json::parse(line);
      if (ready.value("type", "") != "ready" || !ready.value("ok", false) || ready.value("protocol", 0) != 1 ||
          ready.value("max_length", 0) != 128) {
        error =
            "PRISM tokenizer worker failed to initialize: " + ready.value("error", std::string("unsupported protocol"));
        StopTokenizer(worker, true);
        IncludeWorkerDiagnostics(worker, error);
        return false;
      }
    } catch (const std::exception& exception) {
      error = std::string("Invalid PRISM tokenizer ready message: ") + exception.what();
      StopTokenizer(worker, true);
      IncludeWorkerDiagnostics(worker, error);
      return false;
    }
    return true;
  }

  static void StopTokenizer(TokenizerWorker& worker, bool forceTerminate = false) {
    if (worker.StdinWrite != nullptr) {
      CloseHandle(worker.StdinWrite);
      worker.StdinWrite = nullptr;
    }
    if (worker.Process != nullptr) {
      if (forceTerminate) {
        (void)TerminateProcess(worker.Process, 1);
      }
      (void)WaitForSingleObject(worker.Process, INFINITE);
      CloseHandle(worker.Process);
      worker.Process = nullptr;
    }
    if (worker.ProcessThread != nullptr) {
      CloseHandle(worker.ProcessThread);
      worker.ProcessThread = nullptr;
    }
    if (worker.StdoutRead != nullptr) {
      CloseHandle(worker.StdoutRead);
      worker.StdoutRead = nullptr;
    }
    if (worker.StderrDrain.joinable()) {
      worker.StderrDrain.join();
    }
    if (worker.StderrRead != nullptr) {
      CloseHandle(worker.StderrRead);
      worker.StderrRead = nullptr;
    }
  }

  auto Tokenize(TokenizerWorker& worker, uint64_t id, const std::string& prompt, bool directional,
                std::vector<int64_t>& inputIds, std::vector<int64_t>& attentionMask, std::string& error) -> bool {
    if (IsCancelled(id)) {
      return true;
    }
    const std::string request =
        nlohmann::json{{"id", id}, {"prompt", prompt}, {"directional", directional}}.dump() + "\n";
    DWORD written = 0;
    if (!WriteFile(worker.StdinWrite, request.data(), static_cast<DWORD>(request.size()), &written, nullptr) ||
        written != request.size()) {
      error = "Could not send prompt to PRISM tokenizer worker";
      StopTokenizer(worker, true);
      IncludeWorkerDiagnostics(worker, error);
      return false;
    }
    std::string line;
    if (!ReadLine(worker, line)) {
      error = "PRISM tokenizer worker exited without a token response";
      StopTokenizer(worker, true);
      IncludeWorkerDiagnostics(worker, error);
      return false;
    }
    try {
      const auto response = nlohmann::json::parse(line);
      if (response.value("type", "") != "tokens" || response.value("id", uint64_t{0}) != id ||
          response.value("directional", !directional) != directional) {
        error = response.value("error", std::string("tokenizer response type or request id mismatch"));
        if (response.value("type", "") != "error") {
          StopTokenizer(worker, true);
        }
        IncludeWorkerDiagnostics(worker, error);
        return false;
      }
      inputIds = response.at("input_ids").get<std::vector<int64_t>>();
      attentionMask = response.at("attention_mask").get<std::vector<int64_t>>();
      if (inputIds.size() != 128 || attentionMask.size() != 128 ||
          std::ranges::any_of(inputIds, [](int64_t value) { return value < 0; }) ||
          std::ranges::any_of(attentionMask, [](int64_t value) { return value != 0 && value != 1; })) {
        error = "Tokenizer returned an invalid fixed-length tensor";
        StopTokenizer(worker, true);
        IncludeWorkerDiagnostics(worker, error);
        return false;
      }
    } catch (const std::exception& exception) {
      error = std::string("Invalid PRISM tokenizer token response: ") + exception.what();
      StopTokenizer(worker, true);
      IncludeWorkerDiagnostics(worker, error);
      return false;
    }
    return true;
  }

  static auto TensorFloats(Ort::Value& value, std::span<const int64_t> expectedShape, std::string_view name)
      -> std::vector<float> {
    const auto info = value.GetTensorTypeAndShapeInfo();
    const auto actualShape = info.GetShape();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        !std::ranges::equal(actualShape, expectedShape)) {
      throw std::runtime_error(std::string("Unexpected PRISM tensor contract for ") + std::string(name));
    }
    const float* data = value.GetTensorData<float>();
    if (!std::ranges::all_of(std::span(data, info.GetElementCount()),
                             [](float element) { return std::isfinite(element); })) {
      throw std::runtime_error(std::string("PRISM ONNX tensor contains non-finite values: ") + std::string(name));
    }
    return {data, data + info.GetElementCount()};
  }
#  else
  auto StartTokenizer(std::string& error) -> bool {
    error = "PRISM tokenizer process integration is currently supported only on Windows";
    return false;
  }
  auto Tokenize(uint64_t, const std::string&, bool, std::vector<int64_t>&, std::vector<int64_t>&, std::string& error)
      -> bool {
    error = "PRISM tokenizer process integration is currently supported only on Windows";
    return false;
  }
#  endif

  void Process(uint64_t id, const std::string& prompt, bool directional, RuntimeModels& models
#  ifdef _WIN32
               ,
               TokenizerWorker& tokenizer
#  endif
  ) {
    if (IsCancelled(id)) {
      SetStatus(id, PrismNdfInferencePhase::Cancelled, 1.0F, "Generation cancelled");
      return;
    }
    try {
      SetStatus(id, PrismNdfInferencePhase::Tokenizing, 0.05F);
      std::vector<int64_t> inputIds;
      std::vector<int64_t> attentionMask;
      std::string error;
#  ifdef _WIN32
      if (!Tokenize(tokenizer, id, prompt, directional, inputIds, attentionMask, error)) {
#  else
      if (!Tokenize(id, prompt, directional, inputIds, attentionMask, error)) {
#  endif
        SetStatus(id, PrismNdfInferencePhase::Failed, 1.0F, std::move(error));
        return;
      }
      if (IsCancelled(id)) {
        SetStatus(id, PrismNdfInferencePhase::Cancelled, 1.0F, "Generation cancelled");
        return;
      }

      const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
      const std::array<int64_t, 2> tokenShape{1, 128};
      const std::array<int64_t, 2> embeddingShape{1, 1024};
      const std::array<int64_t, 2> latentShape{1, 16};
      const std::array<int64_t, 2> controlsShape{1, 7};
      const std::array<int64_t, 2> gridShape{1, 16384};
      auto controlFeatures = Runtime::Detail::ParsePrismControlFeatures(prompt, directional);

      SetStatus(id, PrismNdfInferencePhase::Encoding, 0.3F);
      Ort::Value idsTensor = Ort::Value::CreateTensor<int64_t>(memory, inputIds.data(), inputIds.size(),
                                                               tokenShape.data(), tokenShape.size());
      Ort::Value maskTensor = Ort::Value::CreateTensor<int64_t>(memory, attentionMask.data(), attentionMask.size(),
                                                                tokenShape.data(), tokenShape.size());
      const char* encoderInputs[] = {"input_ids", "attention_mask"};
      const char* encoderOutputs[] = {"normalized_embedding"};
      std::array<Ort::Value, 2> encoderTensors{std::move(idsTensor), std::move(maskTensor)};
      auto encoded =
          models.Encoder->Run(Ort::RunOptions{nullptr}, encoderInputs, encoderTensors.data(), 2, encoderOutputs, 1);
      auto embedding = TensorFloats(encoded.front(), embeddingShape, "normalized_embedding");
      if (IsCancelled(id)) {
        SetStatus(id, PrismNdfInferencePhase::Cancelled, 1.0F, "Generation cancelled");
        return;
      }

      SetStatus(id, PrismNdfInferencePhase::Retrieving, 0.5F);
      Ort::Value embeddingTensor = Ort::Value::CreateTensor<float>(memory, embedding.data(), embedding.size(),
                                                                   embeddingShape.data(), embeddingShape.size());
      Ort::Value controlsTensor = Ort::Value::CreateTensor<float>(memory, controlFeatures.data(), controlFeatures.size(),
                                                                  controlsShape.data(), controlsShape.size());
      const char* predictorInputs[] = {"embedding", "control_features"};
      const char* predictorOutputs[] = {"predicted_latent"};
      std::array<Ort::Value, 2> predictorTensors{std::move(embeddingTensor), std::move(controlsTensor)};
      auto predicted = models.Predictor->Run(Ort::RunOptions{nullptr}, predictorInputs, predictorTensors.data(), 2,
                                            predictorOutputs, 1);
      auto latent = TensorFloats(predicted.front(), latentShape, "predicted_latent");
      if (IsCancelled(id)) {
        SetStatus(id, PrismNdfInferencePhase::Cancelled, 1.0F, "Generation cancelled");
        return;
      }

      SetStatus(id, PrismNdfInferencePhase::Decoding, 0.7F);
      Ort::Value latentTensor =
          Ort::Value::CreateTensor<float>(memory, latent.data(), latent.size(), latentShape.data(), latentShape.size());
      const char* decoderInputs[] = {"latent"};
      const char* decoderOutputs[] = {"ndf_grid_flat"};
      auto decoded = models.Decoder->Run(Ort::RunOptions{nullptr}, decoderInputs, &latentTensor, 1, decoderOutputs, 1);
      auto grid = TensorFloats(decoded.front(), gridShape, "ndf_grid_flat");
      if (IsCancelled(id)) {
        SetStatus(id, PrismNdfInferencePhase::Cancelled, 1.0F, "Generation cancelled");
        return;
      }

      SetStatus(id, PrismNdfInferencePhase::Fitting, 0.9F);
      auto data = std::make_shared<PrismNdfData>();
      if (!Renderer::FitPrismNdfPayload(grid, *data, error) ||
          !Renderer::ComputePrismProjectedEnergyNormalization(grid, data->normalization, error)) {
        SetStatus(id, PrismNdfInferencePhase::Failed, 1.0F, std::move(error));
        return;
      }
      if (IsCancelled(id)) {
        SetStatus(id, PrismNdfInferencePhase::Cancelled, 1.0F, "Generation cancelled");
        return;
      }
      SetStatus(id, PrismNdfInferencePhase::Completed, 1.0F, {}, std::move(data));
    } catch (const Ort::Exception& exception) {
      SetStatus(id, PrismNdfInferencePhase::Failed, 1.0F,
                std::string("PRISM ONNX inference failed: ") + exception.what());
    } catch (const std::exception& exception) {
      SetStatus(id, PrismNdfInferencePhase::Failed, 1.0F, std::string("PRISM generation failed: ") + exception.what());
    }
  }

  void WorkerMain() {
    RuntimeModels models;
    std::string initializationError;
#  ifdef _WIN32
    TokenizerWorker tokenizer;
#  endif
    try {
#  ifdef _WIN32
      if (!StartTokenizer(tokenizer, initializationError)) {
        throw std::runtime_error(initializationError);
      }
#  else
      if (!StartTokenizer(initializationError)) {
        throw std::runtime_error(initializationError);
      }
#  endif
      InitializeModels(models);
    } catch (const Ort::Exception& exception) {
      initializationError = std::string("PRISM ONNX runtime initialization failed: ") + exception.what();
    } catch (const std::exception& exception) {
      initializationError = std::string("PRISM inference service initialization failed: ") + exception.what();
    }
    {
      std::lock_guard lock(Mutex);
      InitializationError = initializationError;
      RuntimeInitialized = true;
    }

    std::unique_lock lock(Mutex);
    while (true) {
      Wake.wait(lock, [this] { return Stopping || !Pending.empty(); });
      if (Stopping && Pending.empty()) {
        break;
      }
      const auto request = Pending.begin();
      const uint64_t id = request->first;
      const std::string prompt = request->second.first;
      const bool directional = request->second.second;
      Pending.erase(request);
      ActiveRequest = id;
      const std::string currentInitializationError = InitializationError;
      lock.unlock();
      if (!currentInitializationError.empty()) {
        SetStatus(id, PrismNdfInferencePhase::Failed, 1.0F, currentInitializationError);
      } else {
#  ifdef _WIN32
        Process(id, prompt, directional, models, tokenizer);
        if (tokenizer.Process == nullptr) {
          std::lock_guard stateLock(Mutex);
          InitializationError = "PRISM tokenizer worker stopped after a protocol or process error";
        }
#  else
        Process(id, prompt, directional, models);
#  endif
      }
      lock.lock();
      ActiveRequest = 0;
    }
#  ifdef _WIN32
    lock.unlock();
    StopTokenizer(tokenizer);
#  endif
  }
#endif
};

PrismNdfInferenceService::PrismNdfInferenceService(PrismNdfInferenceOptions options)
    : m_Impl(std::make_unique<Impl>(std::move(options))) {}
PrismNdfInferenceService::~PrismNdfInferenceService() = default;

auto PrismNdfInferenceService::Start(std::string_view promptText, bool directional) -> Result<uint64_t> {
#if ENGINE_ENABLE_PRISM_INFERENCE
  if (promptText.empty()) {
    return std::unexpected(MakeError("PRISM prompt must not be empty"));
  }
  if (promptText.size() > size_t{16} * 1024) {
    return std::unexpected(MakeError("PRISM prompt exceeds the 16 KiB input limit"));
  }
  std::string prompt(promptText);
  const auto isFile = [](const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
  };
  const std::array requiredFiles = {
      m_Impl->Options.pythonExecutable,
      m_Impl->Options.tokenizerWorker,
      m_Impl->Options.modelDirectory / "e5_encoder.onnx",
      m_Impl->Options.modelDirectory / "e5_encoder.onnx.data",
      m_Impl->Options.modelDirectory / "prism_text_predictor_128.onnx",
      m_Impl->Options.modelDirectory / "prism_text_predictor_128.onnx.data",
      m_Impl->Options.modelDirectory / "retriever.onnx.data",
      m_Impl->Options.modelDirectory / "prism_vae_decoder_128.onnx",
      m_Impl->Options.modelDirectory / "prism_vae_decoder_128.onnx.data",
      m_Impl->Options.modelDirectory / "vae_decoder.onnx.data",
  };
  std::ostringstream missingAssets;
  bool hasMissingAssets = false;
  for (const auto& path : requiredFiles) {
    if (!isFile(path)) {
      if (hasMissingAssets) {
        missingAssets << ", ";
      }
      missingAssets << '\'' << path.string() << '\'';
      hasMissingAssets = true;
    }
  }
  if (hasMissingAssets) {
    return std::unexpected(MakeError("PRISM runtime assets are missing. Missing files: " + missingAssets.str() +
                                     ". Stage them with scripts/download_prism_models.py and "
                                     "scripts/stage_prism_runtime.py, then launch the PRISM-enabled editor "
                                     "with its configured runtime root."));
  }
  std::lock_guard lock(m_Impl->Mutex);
  if (m_Impl->Stopping) {
    return std::unexpected(MakeError("PRISM inference service is shutting down"));
  }
  if (m_Impl->RuntimeInitialized && !m_Impl->InitializationError.empty()) {
    return std::unexpected(MakeError(m_Impl->InitializationError));
  }
  for (const auto& [id, state] : m_Impl->States) {
    (void)id;
    if (state.phase != PrismNdfInferencePhase::Completed && state.phase != PrismNdfInferencePhase::Failed &&
        state.phase != PrismNdfInferencePhase::Cancelled) {
      return std::unexpected(MakeError("A PRISM generation request is already active"));
    }
  }
  uint64_t id = ++m_Impl->NextRequestId;
  if (id == 0) {
    id = ++m_Impl->NextRequestId;
  }
  m_Impl->States.clear();
  m_Impl->Cancelled.clear();
  m_Impl->States[id] = PrismNdfInferenceStatus{id,
                                               PrismNdfInferencePhase::Queued,
                                               0.0F,
                                               {},
                                               {},
                                               prompt,
                                               directional,
                                               "PRISM 128x128 release prism-onnx-128x128-v1.0.0; "
                                               "E5 encoder release e5-onnx-v1.0.0 revision "
                                               "f169b11e22de13617baa190a028a32f3493550b6; "
                                               "decoder output ndf_grid_flat float32[1,16384]"};
  m_Impl->Pending.emplace(id, std::pair{std::move(prompt), directional});
  m_Impl->Wake.notify_one();
  return id;
#else
  (void)promptText;
  (void)directional;
  return std::unexpected(MakeError(
      "PRISM generation is unavailable in this build. Configure with ENGINE_ENABLE_PRISM_INFERENCE=ON and the "
      "prism-inference vcpkg feature, then use a package containing the bundled models and Python tokenizer runtime."));
#endif
}

auto PrismNdfInferenceService::Poll(uint64_t requestId) const -> std::optional<PrismNdfInferenceStatus> {
  std::lock_guard lock(m_Impl->Mutex);
  const auto found = m_Impl->States.find(requestId);
  return found == m_Impl->States.end() ? std::nullopt : std::optional<PrismNdfInferenceStatus>(found->second);
}

auto PrismNdfInferenceService::Cancel(uint64_t requestId) -> Status {
#if ENGINE_ENABLE_PRISM_INFERENCE
  std::lock_guard lock(m_Impl->Mutex);
  const auto found = m_Impl->States.find(requestId);
  if (found == m_Impl->States.end()) {
    return std::unexpected(MakeError("Unknown PRISM generation request"));
  }
  if (found->second.phase == PrismNdfInferencePhase::Completed ||
      found->second.phase == PrismNdfInferencePhase::Failed ||
      found->second.phase == PrismNdfInferencePhase::Cancelled) {
    return std::unexpected(MakeError("PRISM generation request is already complete"));
  }
  m_Impl->Cancelled.insert(requestId);
  m_Impl->Wake.notify_one();
  return {};
#else
  (void)requestId;
  return std::unexpected(
      MakeError("PRISM generation is unavailable because this build has ENGINE_ENABLE_PRISM_INFERENCE=OFF"));
#endif
}

auto PrismNdfInferenceService::IsAvailable() noexcept -> bool {
#if ENGINE_ENABLE_PRISM_INFERENCE
  return true;
#else
  return false;
#endif
}

} // namespace Engine
