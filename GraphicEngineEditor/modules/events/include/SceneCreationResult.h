#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

class SceneCreationResult
{
  public:
    enum class State
    {
        Pending,
        Succeeded,
        Failed
    };

    struct Snapshot
    {
        State state = State::Pending;
        std::string error;
    };

    SceneCreationResult() : m_RequestId(s_NextRequestId.fetch_add(1, std::memory_order_relaxed))
    {
    }

    [[nodiscard]] uint64_t GetRequestId() const noexcept
    {
        return m_RequestId;
    }

    void Complete(bool succeeded, std::string error = {})
    {
        std::lock_guard lock(m_Mutex);
        if (m_State != State::Pending)
        {
            return;
        }
        m_State = succeeded ? State::Succeeded : State::Failed;
        m_Error = std::move(error);
    }

    [[nodiscard]] Snapshot GetSnapshot() const
    {
        std::lock_guard lock(m_Mutex);
        return {m_State, m_Error};
    }

  private:
    inline static std::atomic<uint64_t> s_NextRequestId{1};
    const uint64_t m_RequestId;
    mutable std::mutex m_Mutex;
    State m_State = State::Pending;
    std::string m_Error;
};
