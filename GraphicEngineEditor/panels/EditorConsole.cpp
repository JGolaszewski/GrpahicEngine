#include "EditorConsole.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <utility>

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
    case ArgumentType::Signed: return argument.value.signedValue;
    case ArgumentType::Unsigned: return argument.value.unsignedValue;
    case ArgumentType::Floating: return argument.value.floatingValue;
    case ArgumentType::Boolean: return argument.value.booleanValue;
    case ArgumentType::Text: return argument.value.text;
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
                {"frame_index", event.frameIndex},
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

ImVec4 LevelColor(Engine::Log::Level level)
{
    using Engine::Log::Level;
    switch (level)
    {
    case Level::Trace: return ImVec4(0.60f, 0.62f, 0.66f, 1.0f);
    case Level::Debug: return ImVec4(0.70f, 0.72f, 0.76f, 1.0f);
    case Level::Info: return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
    case Level::Warning: return ImVec4(1.0f, 0.82f, 0.20f, 1.0f);
    case Level::Error:
    case Level::Fatal: return ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
    default: return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
    }
}

} // namespace

void EditorConsoleSink::Push(const Engine::Log::LogRecord &record)
{
    auto entry = std::make_shared<EditorConsoleEntry>();
    entry->event = record.event;
    entry->timestamp = FormatTimestamp(record.event.timestampNs);
    entry->source = std::string(record.event.file ? record.event.file : "") + ":" +
                    std::to_string(record.event.line) + " " +
                    (record.event.function ? record.event.function : "");
    entry->message = record.text;
    entry->filterText = entry->message;
    entry->filterText += " ";
    entry->filterText += Engine::Log::ToString(record.event.category);

    if (record.event.level >= Engine::Log::Level::Error && m_PauseOnError.load(std::memory_order_relaxed))
        m_PauseRequested.store(true, std::memory_order_release);

    std::shared_ptr<const EditorConsoleEntry> retired;
    {
        std::lock_guard lock(m_Mutex);
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
    std::array<std::shared_ptr<const EditorConsoleEntry>, Capacity> retired;
    {
        std::lock_guard lock(m_Mutex);
        retired.swap(m_Entries);
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

EditorConsole::EditorConsole() : m_Sink(std::make_shared<EditorConsoleSink>())
{
    m_Snapshot.reserve(EditorConsoleSink::Capacity);
    m_VisibleEntries.reserve(EditorConsoleSink::Capacity);
}

EditorConsole::~EditorConsole()
{
    DetachFromLogger();
}

void EditorConsole::AttachToLogger()
{
    const std::shared_ptr<EditorConsoleSink> sink = m_Sink;
    Engine::Log::SetCallback([sink](const Engine::Log::LogRecord &record) { sink->Push(record); });
}

void EditorConsole::DetachFromLogger()
{
    if (Engine::Log::IsInitialized())
        Engine::Log::SetCallback({});
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

void EditorConsole::UpdateAndRender(bool *isOpen)
{
    if (!isOpen)
        return;

    if (ImGui::Begin("Engine Console", isOpen))
    {
        if (ImGui::Button("Clear"))
            m_Sink->Clear();
        ImGui::SameLine();
        ImGui::Checkbox("Pause Simulation on Error", &m_PauseOnError);
        m_Sink->SetPauseOnError(m_PauseOnError);
        if (m_IsSimulationPaused && m_IsSimulationPaused())
        {
            ImGui::SameLine();
            if (ImGui::Button("Resume Simulation") && m_SetSimulationPaused)
                m_SetSimulationPaused(false);
        }

        ImGui::SetNextItemWidth(-1.0f);
        m_Filter.Draw("Search message or category");

        constexpr const char *levelNames[] = {"Trace", "Debug", "Info", "Warn", "Error", "Fatal"};
        for (size_t i = 0; i < m_VisibleLevels.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Checkbox(levelNames[i], &m_VisibleLevels[i]);
            ImGui::PopID();
            if (i + 1 < m_VisibleLevels.size())
                ImGui::SameLine();
        }

        const uint64_t generation = m_Sink->Generation();
        if (generation != m_SnapshotGeneration)
        {
            m_Sink->CopySnapshot(m_Snapshot);
            m_SnapshotGeneration = generation;
        }
        m_VisibleEntries.clear();
        for (const auto &entry : m_Snapshot)
        {
            if (!entry)
                continue;
            const size_t levelIndex = static_cast<size_t>(entry->event.level);
            if (levelIndex < m_VisibleLevels.size() && m_VisibleLevels[levelIndex] &&
                m_Filter.PassFilter(entry->filterText.c_str()))
                m_VisibleEntries.push_back(entry);
        }
        ImGui::Text("Entries: %zu / %zu", m_Snapshot.size(), EditorConsoleSink::Capacity);

        if (ImGui::BeginChild("LogEntries", ImVec2(0.0f, 0.0f), false,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
            m_AutoScroll = wasAtBottom;
            if (ImGui::BeginTable("LogTable", 5,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_Reorderable | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 62.0f);
                ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.7f);
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
                        ImGui::TextColored(color, "%s", Engine::Log::ToString(event.level));
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(category);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextColored(color, "%s", entry->message.c_str());
                        if (ImGui::BeginPopupContextItem("LogEntryContext"))
                        {
                            if (ImGui::MenuItem("Copy JSON"))
                                ImGui::SetClipboardText(ToJson(event, entry->message).dump(2).c_str());
                            ImGui::EndPopup();
                        }
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextUnformatted(entry->source.c_str());
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            if (m_AutoScroll)
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
        }
        ImGui::EndChild();
    }
    ImGui::End();
}
