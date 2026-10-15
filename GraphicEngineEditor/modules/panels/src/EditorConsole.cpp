#include "panels/include/EditorConsole.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <unordered_map>

#include "platform/include/EditorFileDialogs.h"
#include "runtime/include/EnvironmentVariable.h"

namespace
{
using Json = nlohmann::json;

std::string FormatTimestamp(uint64_t timestampNs)
{
    const std::time_t seconds = static_cast<std::time_t>(timestampNs / 1'000'000'000ULL);
    const uint32_t milliseconds = static_cast<uint32_t>((timestampNs / 1'000'000ULL) % 1000ULL);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &seconds);
#else
    localtime_r(&seconds, &localTime);
#endif
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03u", localTime.tm_hour, localTime.tm_min,
                  localTime.tm_sec, milliseconds);
    return buffer;
}

Json ToJson(const Engine::Log::LogArgument &argument)
{
    using Engine::Log::ArgumentType;
    switch (argument.type)
    {
    case ArgumentType::Signed:
        return argument.value.signedValue;
    case ArgumentType::Unsigned:
        return argument.value.unsignedValue;
    case ArgumentType::Floating:
        return argument.value.floatingValue;
    case ArgumentType::Boolean:
        return argument.value.booleanValue;
    case ArgumentType::Text:
        return argument.value.text;
    }
    return nullptr;
}

Json ToJson(const Engine::Log::LogEvent &event, const std::string &message)
{
    Json payload = Json::object();
    for (size_t i = 0; i < event.payloadCount; ++i)
        payload[event.payload[i].key] = ToJson(event.payload[i].value);

    Json arguments = Json::array();
    for (size_t i = 0; i < event.argumentCount; ++i)
        arguments.push_back(ToJson(event.arguments[i]));

    return Json{{"timestamp_ns", event.timestampNs},
                {"monotonic_ns", event.monotonicNs},
                {"frame_index", event.frameIndex},
                {"operation_id", event.operationId},
                {"level", Engine::Log::ToString(event.level)},
                {"category", Engine::Log::ToString(event.category)},
                {"file", event.file ? event.file : ""},
                {"line", event.line},
                {"function", event.function ? event.function : ""},
                {"error_code", event.errorCode[0] ? Json(event.errorCode) : Json(nullptr)},
                {"message", message},
                {"payload", std::move(payload)},
                {"arguments", std::move(arguments)},
                {"thread_id", event.threadId}};
}

void RedactJsonStrings(Json &value)
{
    if (value.is_object())
    {
        for (auto &[key, child] : value.items())
        {
            (void)key;
            RedactJsonStrings(child);
        }
        return;
    }
    if (value.is_array())
    {
        for (Json &child : value)
            RedactJsonStrings(child);
        return;
    }
    if (!value.is_string())
        return;

    std::string text = value.get<std::string>();
    auto profile = Engine::ReadEnvironmentVariable("USERPROFILE");
    if (!profile || profile->empty())
        profile = Engine::ReadEnvironmentVariable("HOME");
    if (profile && !profile->empty())
    {
        const std::string &prefix = *profile;
        std::string foldedText = text;
        std::string foldedPrefix = prefix;
        std::transform(foldedText.begin(), foldedText.end(), foldedText.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        std::transform(foldedPrefix.begin(), foldedPrefix.end(), foldedPrefix.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        size_t position = 0;
        while ((position = foldedText.find(foldedPrefix, position)) != std::string::npos)
        {
            text.replace(position, prefix.size(), "%USERPROFILE%");
            foldedText.replace(position, prefix.size(), "%USERPROFILE%");
            position += sizeof("%USERPROFILE%") - 1;
        }
    }
    value = std::move(text);
}

ImVec4 LevelColor(Engine::Log::Level level)
{
    using Engine::Log::Level;
    switch (level)
    {
    case Level::Trace:
        return ImVec4(0.60f, 0.62f, 0.66f, 1.0f);
    case Level::Debug:
        return ImVec4(0.70f, 0.72f, 0.76f, 1.0f);
    case Level::Info:
        return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
    case Level::Warning:
        return ImVec4(1.0f, 0.82f, 0.20f, 1.0f);
    case Level::Error:
    case Level::Fatal:
        return ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
    default:
        return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
    }
}

} // namespace

void EditorConsoleSink::Push(const Engine::Log::LogRecord &record)
{
    if (record.event.level >= Engine::Log::Level::Error && m_PauseOnError.load(std::memory_order_relaxed))
        m_PauseRequested.store(true, std::memory_order_release);
    if (m_CapturePaused.load(std::memory_order_acquire))
    {
        m_DroppedWhilePaused.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto entry = std::make_shared<EditorConsoleEntry>();
    entry->event = record.event;
    entry->timestamp = FormatTimestamp(record.event.timestampNs);
    entry->source = std::string(record.event.file ? record.event.file : "") + ":" +
                    std::to_string(record.event.line) + " " +
                    (record.event.function ? record.event.function : "");
    entry->message = record.text;
    entry->filterText = entry->message;
    entry->filterText += ' ';
    entry->filterText += Engine::Log::ToString(record.event.category);
    entry->filterText += ' ';
    entry->filterText += record.event.errorCode;
    entry->filterText += ' ';
    entry->filterText += entry->source;
    entry->filterText += ' ';
    entry->filterText += std::to_string(record.event.threadId);

    std::shared_ptr<const EditorConsoleEntry> retired;
    {
        std::lock_guard lock(m_Mutex);
        if (m_Size == Capacity)
            m_OverwrittenEntries.fetch_add(1, std::memory_order_relaxed);
        retired = std::move(m_Entries[m_WriteIndex]);
        m_Entries[m_WriteIndex] = std::move(entry);
        m_WriteIndex = (m_WriteIndex + 1) % Capacity;
        m_Size = (std::min)(m_Size + 1, Capacity);
    }
    m_Generation.fetch_add(1, std::memory_order_release);
}

void EditorConsoleSink::CopySnapshot(std::vector<std::shared_ptr<const EditorConsoleEntry>> &snapshot) const
{
    std::lock_guard lock(m_Mutex);
    snapshot.clear();
    snapshot.reserve(m_Size);
    const size_t first = (m_WriteIndex + Capacity - m_Size) % Capacity;
    for (size_t i = 0; i < m_Size; ++i)
        snapshot.push_back(m_Entries[(first + i) % Capacity]);
}

uint64_t EditorConsoleSink::Generation() const noexcept
{
    return m_Generation.load(std::memory_order_acquire);
}

void EditorConsoleSink::Clear()
{
    std::vector<std::shared_ptr<const EditorConsoleEntry>> retired;
    retired.reserve(Capacity);
    {
        std::lock_guard lock(m_Mutex);
        for (auto &entry : m_Entries)
            retired.push_back(std::move(entry));
        m_WriteIndex = 0;
        m_Size = 0;
    }
    m_Generation.fetch_add(1, std::memory_order_release);
}

void EditorConsoleSink::SetPauseOnError(bool enabled) noexcept
{
    m_PauseOnError.store(enabled, std::memory_order_relaxed);
}

bool EditorConsoleSink::ConsumePauseRequest() noexcept
{
    return m_PauseRequested.exchange(false, std::memory_order_acq_rel);
}

void EditorConsoleSink::SetCapturePaused(bool paused) noexcept
{
    m_CapturePaused.store(paused, std::memory_order_release);
}

bool EditorConsoleSink::IsCapturePaused() const noexcept
{
    return m_CapturePaused.load(std::memory_order_acquire);
}

uint64_t EditorConsoleSink::DroppedWhilePaused() const noexcept
{
    return m_DroppedWhilePaused.load(std::memory_order_relaxed);
}

uint64_t EditorConsoleSink::OverwrittenEntries() const noexcept
{
    return m_OverwrittenEntries.load(std::memory_order_relaxed);
}

EditorConsole::EditorConsole() : m_Sink(std::make_shared<EditorConsoleSink>())
{
    m_Snapshot.reserve(EditorConsoleSink::Capacity);
    m_VisibleEntries.reserve(EditorConsoleSink::Capacity);
    m_VisibleCategories.fill(true);
}

EditorConsole::~EditorConsole()
{
    DetachFromLogger();
}

void EditorConsole::AttachToLogger()
{
    if (m_LoggerSubscription != 0)
        return;
    m_RuntimeMinimumLevel = Engine::Log::GetMinimumLevel();
    m_RuntimeCategoryMinimumLevel = Engine::Log::GetCategoryMinimumLevel(Engine::Log::Category::Core);
    const std::shared_ptr<EditorConsoleSink> sink = m_Sink;
    m_LoggerSubscription =
        Engine::Log::SubscribeCallback([sink](const Engine::Log::LogRecord &record) { sink->Push(record); });
}

void EditorConsole::DetachFromLogger()
{
    Engine::Log::UnsubscribeCallback(m_LoggerSubscription);
    m_LoggerSubscription = 0;
}

bool EditorConsole::ConsumePauseRequest() noexcept
{
    return m_Sink->ConsumePauseRequest();
}

void EditorConsole::SetSimulationStateCallbacks(std::function<bool()> isPaused,
                                                std::function<void(bool)> setPaused)
{
    m_IsSimulationPaused = std::move(isPaused);
    m_SetSimulationPaused = std::move(setPaused);
}

void EditorConsole::SetDiagnosticContextProvider(std::function<std::string()> provider,
                                                 std::function<uint64_t()> currentFrameProvider)
{
    m_DiagnosticContextProvider = std::move(provider);
    m_CurrentFrameProvider = std::move(currentFrameProvider);
}

void EditorConsole::UpdateAndRender(bool *isOpen)
{
    if (!isOpen)
        return;

    if (ImGui::Begin("Engine Console", isOpen))
    {
        const Engine::Log::HealthSnapshot health = Engine::Log::GetHealthSnapshot();
        const uint64_t generation = m_Sink->Generation();
        if (generation != m_SnapshotGeneration)
        {
            m_Sink->CopySnapshot(m_Snapshot);
            m_SnapshotGeneration = generation;
        }
        const char *fileStatus = health.fileStatus == Engine::Log::FileSinkStatus::Ready     ? "ready"
                                 : health.fileStatus == Engine::Log::FileSinkStatus::Failed  ? "failed"
                                 : health.fileStatus == Engine::Log::FileSinkStatus::Opening ? "opening"
                                                                                             : "disabled";
        if (ImGui::Button(m_CapturePaused ? "Resume Capture" : "Pause Capture"))
        {
            m_CapturePaused = !m_CapturePaused;
            m_Sink->SetCapturePaused(m_CapturePaused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            m_Sink->Clear();
            m_SelectedEntry.reset();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Follow latest", &m_AutoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Redact user paths on export", &m_RedactPaths);
        ImGui::SameLine();
        ImGui::Checkbox("Pause simulation on error", &m_PauseOnError);
        m_Sink->SetPauseOnError(m_PauseOnError);

        if (m_IsSimulationPaused && m_IsSimulationPaused())
        {
            ImGui::SameLine();
            if (ImGui::Button("Resume Simulation") && m_SetSimulationPaused)
                m_SetSimulationPaused(false);
        }

        ImGui::Text("Logger %s | queue %zu/%zu (high %zu) | drops %llu queue / %llu callbacks | sink errors "
                    "%llu | callback errors %llu",
                    fileStatus, health.queueDepth, health.queueCapacity, health.queueHighWaterMark,
                    static_cast<unsigned long long>(health.queueDroppedEvents),
                    static_cast<unsigned long long>(health.callbackDroppedEvents),
                    static_cast<unsigned long long>(health.fileFailedEvents),
                    static_cast<unsigned long long>(health.callbackFailedEvents));
        ImGui::Text("Retained %zu/%zu | ring overwrites %llu | dropped while paused %llu | accepted/written "
                    "%llu/%llu | breadcrumbs overwritten %llu",
                    m_Snapshot.size(), EditorConsoleSink::Capacity,
                    static_cast<unsigned long long>(m_Sink->OverwrittenEntries()),
                    static_cast<unsigned long long>(m_Sink->DroppedWhilePaused()),
                    static_cast<unsigned long long>(health.acceptedEvents),
                    static_cast<unsigned long long>(health.writtenEvents),
                    static_cast<unsigned long long>(health.breadcrumbOverwrittenEvents));
        if (!health.jsonlPath.empty())
            ImGui::Text("JSONL: %s | session: %s", health.jsonlPath.generic_string().c_str(),
                        health.sessionId.c_str());
        if (!health.lastSinkError.empty())
            ImGui::TextWrapped("Log sink: %s", health.lastSinkError.c_str());

        constexpr const char *runtimeLevels[] = {"Trace", "Debug", "Info", "Warning",
                                                 "Error", "Fatal", "None"};
        int runtimeLevel = static_cast<int>(m_RuntimeMinimumLevel);
        if (ImGui::Combo("Runtime minimum", &runtimeLevel, runtimeLevels, 7))
        {
            m_RuntimeMinimumLevel = static_cast<Engine::Log::Level>(runtimeLevel);
            Engine::Log::SetMinimumLevel(m_RuntimeMinimumLevel);
        }
        constexpr const char *categoryNames[] = {"Core",  "Editor", "Physics", "Render", "Backend",
                                                 "Audio", "ECS",    "Network", "Asset",  "Diagnostics"};
        constexpr Engine::Log::Category categories[] = {
            Engine::Log::Category::Core,       Engine::Log::Category::Editor,  Engine::Log::Category::Physics,
            Engine::Log::Category::Render,     Engine::Log::Category::Backend, Engine::Log::Category::Audio,
            Engine::Log::Category::ECS,        Engine::Log::Category::Network, Engine::Log::Category::Asset,
            Engine::Log::Category::Diagnostics};
        if (ImGui::Combo("Runtime category", &m_RuntimeCategoryIndex, categoryNames, 10))
            m_RuntimeCategoryMinimumLevel =
                Engine::Log::GetCategoryMinimumLevel(categories[m_RuntimeCategoryIndex]);
        int categoryLevel = static_cast<int>(m_RuntimeCategoryMinimumLevel);
        if (ImGui::Combo("Category minimum", &categoryLevel, runtimeLevels, 7))
        {
            m_RuntimeCategoryMinimumLevel = static_cast<Engine::Log::Level>(categoryLevel);
            Engine::Log::SetCategoryMinimumLevel(categories[m_RuntimeCategoryIndex],
                                                 m_RuntimeCategoryMinimumLevel);
        }

        ImGui::SetNextItemWidth(-1.0f);
        m_Filter.Draw("Search message, category, code, source, or thread");

        if (ImGui::BeginCombo("Categories", "Choose categories"))
        {
            if (ImGui::SmallButton("All"))
                m_VisibleCategories.fill(true);
            ImGui::SameLine();
            if (ImGui::SmallButton("None"))
                m_VisibleCategories.fill(false);
            for (size_t i = 0; i < Engine::Log::CategoryCount; ++i)
            {
                const auto category = static_cast<Engine::Log::Category>(i);
                ImGui::Checkbox(Engine::Log::ToString(category), &m_VisibleCategories[i]);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputText("Error code", m_ErrorCodeFilter, sizeof(m_ErrorCodeFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputText("Source", m_SourceFilter, sizeof(m_SourceFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::InputText("Thread", m_ThreadFilter, sizeof(m_ThreadFilter));

        ImGui::Checkbox("Frame range", &m_FrameRangeEnabled);
        if (m_FrameRangeEnabled)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputScalar("From", ImGuiDataType_U64, &m_FrameRangeMinimum);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputScalar("To", ImGuiDataType_U64, &m_FrameRangeMaximum);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Recent time", &m_TimeRangeEnabled);
        if (m_TimeRangeEnabled)
        {
            constexpr const char *timeRanges[] = {"1 sec", "5 sec", "30 sec", "5 min"};
            int timeRangeIndex = m_TimeRangeSeconds == 1
                                     ? 0
                                     : (m_TimeRangeSeconds == 5 ? 1 : (m_TimeRangeSeconds == 30 ? 2 : 3));
            ImGui::SameLine();
            if (ImGui::Combo("##TimeRange", &timeRangeIndex, timeRanges, 4))
                m_TimeRangeSeconds =
                    timeRangeIndex == 0 ? 1 : (timeRangeIndex == 1 ? 5 : (timeRangeIndex == 2 ? 30 : 300));
        }

        constexpr const char *quickFilters[] = {"All",     "Errors",          "Current frame",
                                                "Backend", "Asset operation", "Recent regression"};
        ImGui::SetNextItemWidth(170.0f);
        ImGui::Combo("Quick filter", &m_QuickFilter, quickFilters, 6);

        constexpr const char *levelNames[] = {"Trace", "Debug", "Info", "Warn", "Error", "Fatal"};
        for (size_t i = 0; i < m_VisibleLevels.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Checkbox(levelNames[i], &m_VisibleLevels[i]);
            ImGui::PopID();
            if (i + 1 < m_VisibleLevels.size())
                ImGui::SameLine();
        }

        m_VisibleEntries.clear();
        std::array<size_t, 6> levelCounts{};
        std::array<size_t, Engine::Log::CategoryCount> categoryCounts{};
        const uint64_t currentFrame = m_CurrentFrameProvider ? m_CurrentFrameProvider() : UINT64_MAX;
        const uint64_t nowMonotonicNs =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
        const uint64_t timeRangeNs = static_cast<uint64_t>(m_TimeRangeSeconds) * 1'000'000'000ULL;
        const uint64_t oldestTimeNs =
            m_TimeRangeEnabled && nowMonotonicNs > timeRangeNs ? nowMonotonicNs - timeRangeNs : 0;
        for (const auto &entry : m_Snapshot)
        {
            if (!entry)
                continue;
            const size_t levelIndex = static_cast<size_t>(entry->event.level);
            const size_t categoryIndex = static_cast<size_t>(entry->event.category);
            if (levelIndex < levelCounts.size())
                ++levelCounts[levelIndex];
            if (categoryIndex < categoryCounts.size())
                ++categoryCounts[categoryIndex];

            if (levelIndex >= m_VisibleLevels.size() || !m_VisibleLevels[levelIndex] ||
                categoryIndex >= m_VisibleCategories.size() || !m_VisibleCategories[categoryIndex] ||
                !m_Filter.PassFilter(entry->filterText.c_str()))
                continue;
            if (m_ErrorCodeFilter[0] &&
                std::string_view(entry->event.errorCode).find(m_ErrorCodeFilter) == std::string_view::npos)
                continue;
            if (m_SourceFilter[0] && entry->source.find(m_SourceFilter) == std::string::npos)
                continue;
            if (m_ThreadFilter[0] &&
                std::to_string(entry->event.threadId).find(m_ThreadFilter) == std::string::npos)
                continue;
            if (m_FrameRangeEnabled && (entry->event.frameIndex < m_FrameRangeMinimum ||
                                        entry->event.frameIndex > m_FrameRangeMaximum))
                continue;
            if (m_TimeRangeEnabled && entry->event.monotonicNs < oldestTimeNs)
                continue;

            const bool isError = entry->event.level >= Engine::Log::Level::Error;
            const bool isCurrentFrame = entry->event.frameIndex == currentFrame;
            const bool isBackend = entry->event.category == Engine::Log::Category::Backend;
            const bool isAsset = entry->event.category == Engine::Log::Category::Asset;
            const std::string_view errorCode(entry->event.errorCode);
            const bool isRegression = errorCode.find("regression") != std::string_view::npos ||
                                      errorCode.find("frame_budget") != std::string_view::npos;
            if ((m_QuickFilter == 1 && !isError) || (m_QuickFilter == 2 && !isCurrentFrame) ||
                (m_QuickFilter == 3 && !isBackend) || (m_QuickFilter == 4 && !isAsset) ||
                (m_QuickFilter == 5 && !isRegression))
                continue;
            m_VisibleEntries.push_back(entry);
        }
        ImGui::Text("Visible %zu / retained %zu | levels: T %zu D %zu I %zu W %zu E %zu F %zu",
                    m_VisibleEntries.size(), m_Snapshot.size(), levelCounts[0], levelCounts[1],
                    levelCounts[2], levelCounts[3], levelCounts[4], levelCounts[5]);
        if (ImGui::TreeNode("Category counts"))
        {
            for (size_t i = 0; i < categoryCounts.size(); ++i)
            {
                ImGui::Text("%s: %zu", Engine::Log::ToString(static_cast<Engine::Log::Category>(i)),
                            categoryCounts[i]);
                if ((i + 1) % 4 != 0 && i + 1 < categoryCounts.size())
                    ImGui::SameLine();
            }
            ImGui::TreePop();
        }
        if (ImGui::Button("Export visible JSONL"))
        {
            m_ExportError.clear();
            const auto path =
                EditorPlatform::ChooseLogExportFile(std::filesystem::current_path() / "logs.jsonl");
            if (path)
            {
                std::ofstream output(*path, std::ios::out | std::ios::trunc);
                if (!output)
                    m_ExportError = "Could not open the selected file for writing.";
                else
                {
                    for (const auto &entry : m_VisibleEntries)
                    {
                        Json json = ToJson(entry->event, entry->message);
                        if (m_RedactPaths)
                            RedactJsonStrings(json);
                        output << json.dump() << '\n';
                    }
                    m_ExportError = output ? std::string{} : "Writing the JSONL export failed.";
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy diagnostic bundle"))
        {
            Json bundle{{"schema", "graphic-engine-diagnostic-bundle-v1"},
                        {"redacted_user_paths", m_RedactPaths},
                        {"session_id", health.sessionId},
                        {"capture_paused", m_CapturePaused},
                        {"dropped_while_paused", m_Sink->DroppedWhilePaused()},
                        {"ring_overwritten_entries", m_Sink->OverwrittenEntries()},
                        {"logger_health",
                         {{"file_status", fileStatus},
                          {"jsonl_path", health.jsonlPath.generic_string()},
                          {"breadcrumb_path", health.breadcrumbPath.generic_string()},
                          {"accepted", health.acceptedEvents},
                          {"written", health.writtenEvents},
                          {"queue_depth", health.queueDepth},
                          {"queue_capacity", health.queueCapacity},
                          {"queue_dropped", health.queueDroppedEvents},
                          {"callback_dropped", health.callbackDroppedEvents},
                          {"file_failures", health.fileFailedEvents},
                          {"callback_failures", health.callbackFailedEvents},
                          {"last_sink_error", health.lastSinkError}}},
                        {"recent_records", Json::array()}};
            const std::vector<Engine::Log::LogRecord> breadcrumbs = Engine::Log::GetRecentRecords();
            const size_t bundleStart = breadcrumbs.size() > 200 ? breadcrumbs.size() - 200 : 0;
            for (size_t i = bundleStart; i < breadcrumbs.size(); ++i)
            {
                Json record = ToJson(breadcrumbs[i].event, breadcrumbs[i].text);
                if (m_RedactPaths)
                    RedactJsonStrings(record);
                bundle["recent_records"].push_back(std::move(record));
            }
            if (m_DiagnosticContextProvider)
            {
                try
                {
                    bundle["application"] = Json::parse(m_DiagnosticContextProvider());
                }
                catch (...)
                {
                    bundle["application"] = {{"error", "Application diagnostics were unavailable."}};
                }
            }
            if (m_RedactPaths)
                RedactJsonStrings(bundle);
            ImGui::SetClipboardText(bundle.dump(2).c_str());
            m_ExportError.clear();
        }
        if (!m_ExportError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_ExportError.c_str());

        if (ImGui::CollapsingHeader("Repeated errors by code"))
        {
            struct ErrorGroup
            {
                size_t Count = 0;
                uint64_t FirstFrame = UINT64_MAX;
                uint64_t LastFrame = 0;
            };
            std::unordered_map<std::string, ErrorGroup> groups;
            for (const auto &entry : m_Snapshot)
            {
                if (entry->event.level < Engine::Log::Level::Error || !entry->event.errorCode[0])
                    continue;
                ErrorGroup &group = groups[entry->event.errorCode];
                ++group.Count;
                group.FirstFrame = (std::min)(group.FirstFrame, entry->event.frameIndex);
                group.LastFrame = (std::max)(group.LastFrame, entry->event.frameIndex);
            }
            for (const auto &[code, group] : groups)
            {
                if (group.Count > 1)
                {
                    ImGui::PushID(code.c_str());
                    if (ImGui::SmallButton(code.c_str()))
                    {
                        std::fill(m_ErrorCodeFilter, m_ErrorCodeFilter + sizeof(m_ErrorCodeFilter), '\0');
                        std::copy_n(code.begin(), (std::min)(code.size(), sizeof(m_ErrorCodeFilter) - 1),
                                    m_ErrorCodeFilter);
                    }
                    ImGui::SameLine();
                    ImGui::Text("%zu occurrences, frames %llu–%llu", group.Count,
                                static_cast<unsigned long long>(group.FirstFrame),
                                static_cast<unsigned long long>(group.LastFrame));
                    ImGui::PopID();
                }
            }
        }

        if (ImGui::BeginChild("LogEntries", ImVec2(0.0f, m_SelectedEntry ? -190.0f : 0.0f), false,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            if (ImGui::BeginTable("LogTable", 8,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_Reorderable | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 62.0f);
                ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 62.0f);
                ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn("Error code", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(m_VisibleEntries.size()));
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        const auto &entry = m_VisibleEntries[static_cast<size_t>(row)];
                        const auto &event = entry->event;
                        const char *category = Engine::Log::ToString(event.category);
                        const ImVec4 color = LevelColor(event.level);
                        ImGui::PushID(row);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(entry->timestamp.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%llu", static_cast<unsigned long long>(event.frameIndex));
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextColored(color, "%s", Engine::Log::ToString(event.level));
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(category);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextUnformatted(event.errorCode);
                        ImGui::TableSetColumnIndex(5);
                        if (ImGui::Selectable(entry->message.c_str(), m_SelectedEntry == entry,
                                              ImGuiSelectableFlags_SpanAllColumns))
                            m_SelectedEntry = entry;
                        ImGui::TableSetColumnIndex(6);
                        ImGui::TextUnformatted(entry->source.c_str());
                        ImGui::TableSetColumnIndex(7);
                        ImGui::Text("%llu", static_cast<unsigned long long>(event.threadId));
                        if (ImGui::BeginPopupContextItem("LogEntryContext"))
                        {
                            if (ImGui::MenuItem("Copy JSON"))
                            {
                                Json json = ToJson(event, entry->message);
                                if (m_RedactPaths)
                                    RedactJsonStrings(json);
                                ImGui::SetClipboardText(json.dump(2).c_str());
                            }
                            if (ImGui::MenuItem("Copy source reference"))
                                ImGui::SetClipboardText((std::string(event.file ? event.file : "") + ":" +
                                                         std::to_string(event.line))
                                                            .c_str());
                            if (ImGui::MenuItem("Open source file") && event.file)
                                EditorPlatform::OpenSourceFile(event.file);
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
                m_AutoScroll = false;
            if (m_AutoScroll)
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
        }
        ImGui::EndChild();

        if (m_SelectedEntry)
        {
            const auto &entry = *m_SelectedEntry;
            ImGui::Separator();
            ImGui::Text("Selected: %s | frame %llu | thread %llu | operation %llu",
                        Engine::Log::ToString(entry.event.level),
                        static_cast<unsigned long long>(entry.event.frameIndex),
                        static_cast<unsigned long long>(entry.event.threadId),
                        static_cast<unsigned long long>(entry.event.operationId));
            ImGui::Text("Error code: %s", entry.event.errorCode[0] ? entry.event.errorCode : "none");
            ImGui::TextWrapped("Source: %s", entry.source.c_str());
            if (entry.event.errorCode[0])
            {
                size_t occurrences = 0;
                uint64_t firstFrame = UINT64_MAX;
                uint64_t lastFrame = 0;
                for (const auto &candidate : m_Snapshot)
                {
                    if (candidate->event.level >= Engine::Log::Level::Error &&
                        candidate->event.errorCode[0] &&
                        std::string_view(candidate->event.errorCode) == entry.event.errorCode)
                    {
                        ++occurrences;
                        firstFrame = (std::min)(firstFrame, candidate->event.frameIndex);
                        lastFrame = (std::max)(lastFrame, candidate->event.frameIndex);
                    }
                }
                ImGui::Text("Code occurrences: %zu | first frame %llu | last frame %llu", occurrences,
                            static_cast<unsigned long long>(firstFrame),
                            static_cast<unsigned long long>(lastFrame));
            }
            if (ImGui::Button("Copy message"))
                ImGui::SetClipboardText(entry.message.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Copy error code"))
                ImGui::SetClipboardText(entry.event.errorCode);
            ImGui::SameLine();
            if (ImGui::Button("Copy file:line"))
                ImGui::SetClipboardText((std::string(entry.event.file ? entry.event.file : "") + ":" +
                                         std::to_string(entry.event.line))
                                            .c_str());
            ImGui::SameLine();
            if (ImGui::Button("Open source file") && entry.event.file)
                EditorPlatform::OpenSourceFile(entry.event.file);
            Json details = ToJson(entry.event, entry.message);
            if (m_RedactPaths)
                RedactJsonStrings(details);
            ImGui::BeginChild("LogDetails", ImVec2(0.0f, 160.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(details.dump(2).c_str());
            ImGui::EndChild();
        }
    }
    ImGui::End();
}
