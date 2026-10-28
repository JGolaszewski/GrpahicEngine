#include "runtime/include/PrismNdfInferenceService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>

namespace {

auto IsFinitePayload(const Engine::PrismNdfData& payload) -> bool {
  return std::isfinite(payload.normalization) && payload.normalization > 0.0F &&
         std::ranges::all_of(payload.zernikeCoefficients, [](float value) { return std::isfinite(value); }) &&
         std::ranges::all_of(payload.residuals, [](float value) { return std::isfinite(value); });
}

} // namespace

auto Run(int argc, char** argv) -> int {
  if (argc != 2) {
    std::cerr << "Usage: PrismNdfPackageSmoke <package-bin-directory>\n";
    return 2;
  }

  const std::filesystem::path packageBin = std::filesystem::absolute(argv[1]);
  Engine::PrismNdfInferenceService service({
      .modelDirectory = packageBin / "models" / "prism",
      .pythonExecutable = packageBin / "python" / "python.exe",
      .tokenizerWorker = packageBin / "python" / "prism_tokenizer_worker.py",
  });
  constexpr std::string_view prompt = "polished titanium with fine brushed anisotropic highlights";
  auto request = service.Start(prompt, true);
  if (!request) {
    std::cerr << "Could not start packaged PRISM generation: " << request.error().Message << '\n';
    return 1;
  }

  constexpr auto timeout = std::chrono::minutes(15);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto status = service.Poll(*request);
    if (!status) {
      std::cerr << "PRISM generation request disappeared before reaching a terminal state\n";
      return 1;
    }
    if (status->phase == Engine::PrismNdfInferencePhase::Failed ||
        status->phase == Engine::PrismNdfInferencePhase::Cancelled) {
      std::cerr << "Packaged PRISM generation failed: " << status->message << '\n';
      return 1;
    }
    if (status->phase == Engine::PrismNdfInferencePhase::Completed) {
      if (!status->result || status->prompt != prompt || !status->directional ||
          status->modelProvenance.empty() || !IsFinitePayload(*status->result)) {
        std::cerr << "Packaged PRISM generation returned incomplete or invalid material data\n";
        return 1;
      }
      std::cout << "Packaged PRISM inference generated a finite 45-term, 4096-residual payload; normalization="
                << status->result->normalization << ", provenance=" << status->modelProvenance << '\n';
      return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cerr << "Packaged PRISM generation exceeded the 15-minute timeout\n";
  return 1;
}

auto main(int argc, char** argv) -> int {
  try {
    return Run(argc, argv);
  } catch (const std::exception& exception) {
    std::fprintf(stderr, "Packaged PRISM smoke test threw an exception: %s\n", exception.what());
    return 1;
  } catch (...) {
    std::fputs("Packaged PRISM smoke test threw an unknown exception\n", stderr);
    return 1;
  }
}
