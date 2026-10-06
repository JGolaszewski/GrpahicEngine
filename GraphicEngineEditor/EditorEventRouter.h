#pragma once

#include "panels/EditorPanel.h"

#include <cstdint>
#include <vector>

class EditorEventRouter
{
  public:
    class Connection
    {
      public:
        Connection() = default;
        Connection(EditorEventRouter *router, uint64_t id) noexcept : m_Router(router), m_Id(id)
        {
        }
        ~Connection();

        Connection(const Connection &) = delete;
        Connection &operator=(const Connection &) = delete;
        Connection(Connection &&other) noexcept;
        Connection &operator=(Connection &&other) noexcept;

        void Reset() noexcept;

      private:
        EditorEventRouter *m_Router = nullptr;
        uint64_t m_Id = 0;
    };

    using RequestHandler = EditorPanel::RequestHandler;

    EditorEventRouter() = default;
    EditorEventRouter(const EditorEventRouter &) = delete;
    EditorEventRouter &operator=(const EditorEventRouter &) = delete;

    Connection Subscribe(EditorPanel &panel, RequestHandler requestHandler);
    void DispatchInput(Engine::Events::Event &event);
    void NotifyPanels(Engine::Events::Event &event);

  private:
    struct Subscriber
    {
        uint64_t Id;
        EditorPanel *Panel;
    };

    void Unsubscribe(uint64_t id) noexcept;
    std::vector<uint64_t> SnapshotSubscriberIds() const;
    std::vector<Subscriber> m_Subscribers;
    uint64_t m_NextId = 1;

    friend class Connection;
};
