#include "EditorEventRouter.h"

#include <algorithm>
#include <utility>

EditorEventRouter::Connection::~Connection()
{
    Reset();
}

EditorEventRouter::Connection::Connection(Connection &&other) noexcept
    : m_Router(std::exchange(other.m_Router, nullptr)), m_Id(std::exchange(other.m_Id, 0))
{
}

EditorEventRouter::Connection &EditorEventRouter::Connection::operator=(Connection &&other) noexcept
{
    if (this != &other)
    {
        Reset();
        m_Router = std::exchange(other.m_Router, nullptr);
        m_Id = std::exchange(other.m_Id, 0);
    }
    return *this;
}

void EditorEventRouter::Connection::Reset() noexcept
{
    if (m_Router)
    {
        m_Router->Unsubscribe(m_Id);
        m_Router = nullptr;
        m_Id = 0;
    }
}

EditorEventRouter::Connection EditorEventRouter::Subscribe(EditorPanel &panel, RequestHandler requestHandler)
{
    const uint64_t id = m_NextId++;
    panel.SetRequestSink(std::move(requestHandler));
    m_Subscribers.push_back({id, &panel});
    return Connection(this, id);
}

void EditorEventRouter::DispatchInput(Engine::Events::Event &event)
{
    for (const uint64_t id : SnapshotSubscriberIds())
    {
        const auto found = std::find_if(m_Subscribers.begin(), m_Subscribers.end(),
                                        [id](const Subscriber &subscriber) { return subscriber.Id == id; });
        if (found == m_Subscribers.end())
            continue;
        found->Panel->OnEvent(event);
        if (event.Handled)
            return;
    }
}

void EditorEventRouter::NotifyPanels(Engine::Events::Event &event)
{
    for (const uint64_t id : SnapshotSubscriberIds())
    {
        const auto found = std::find_if(m_Subscribers.begin(), m_Subscribers.end(),
                                        [id](const Subscriber &subscriber) { return subscriber.Id == id; });
        if (found != m_Subscribers.end())
            found->Panel->OnEvent(event);
    }
}

std::vector<uint64_t> EditorEventRouter::SnapshotSubscriberIds() const
{
    std::vector<uint64_t> ids;
    ids.reserve(m_Subscribers.size());
    for (const auto &subscriber : m_Subscribers)
        ids.push_back(subscriber.Id);
    return ids;
}

void EditorEventRouter::Unsubscribe(uint64_t id) noexcept
{
    const auto found = std::find_if(m_Subscribers.begin(), m_Subscribers.end(),
                                    [id](const Subscriber &subscriber) { return subscriber.Id == id; });
    if (found != m_Subscribers.end())
    {
        found->Panel->SetRequestSink({});
        m_Subscribers.erase(found);
    }
}
