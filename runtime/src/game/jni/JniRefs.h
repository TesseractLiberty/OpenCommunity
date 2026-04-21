#pragma once

#include <jni.h>
#include <utility>

class JniLocalFrame {
public:
    JniLocalFrame(JNIEnv* env, jint capacity)
        : m_Env(env), m_Active(env && env->PushLocalFrame(capacity) == 0) {}

    ~JniLocalFrame() {
        Pop();
    }

    JniLocalFrame(const JniLocalFrame&) = delete;
    JniLocalFrame& operator=(const JniLocalFrame&) = delete;

    JniLocalFrame(JniLocalFrame&& other) noexcept
        : m_Env(std::exchange(other.m_Env, nullptr)), m_Active(std::exchange(other.m_Active, false)) {}

    JniLocalFrame& operator=(JniLocalFrame&& other) noexcept {
        if (this != &other) {
            Pop();
            m_Env = std::exchange(other.m_Env, nullptr);
            m_Active = std::exchange(other.m_Active, false);
        }
        return *this;
    }

    bool IsActive() const {
        return m_Active;
    }

    jobject Pop(jobject result = nullptr) {
        if (!m_Env || !m_Active) {
            return result;
        }

        m_Active = false;
        return m_Env->PopLocalFrame(result);
    }

private:
    JNIEnv* m_Env = nullptr;
    bool m_Active = false;
};

template <typename T = jobject>
class JniLocalRef {
public:
    JniLocalRef() = default;

    JniLocalRef(JNIEnv* env, T ref)
        : m_Env(env), m_Ref(ref) {}

    ~JniLocalRef() {
        Reset();
    }

    JniLocalRef(const JniLocalRef&) = delete;
    JniLocalRef& operator=(const JniLocalRef&) = delete;

    JniLocalRef(JniLocalRef&& other) noexcept
        : m_Env(std::exchange(other.m_Env, nullptr)), m_Ref(std::exchange(other.m_Ref, nullptr)) {}

    JniLocalRef& operator=(JniLocalRef&& other) noexcept {
        if (this != &other) {
            Reset();
            m_Env = std::exchange(other.m_Env, nullptr);
            m_Ref = std::exchange(other.m_Ref, nullptr);
        }
        return *this;
    }

    T Get() const {
        return m_Ref;
    }

    explicit operator bool() const {
        return m_Ref != nullptr;
    }

    void Reset(T ref = nullptr) {
        if (m_Env && m_Ref) {
            m_Env->DeleteLocalRef(reinterpret_cast<jobject>(m_Ref));
        }
        m_Ref = ref;
    }

    T Release() {
        return std::exchange(m_Ref, nullptr);
    }

private:
    JNIEnv* m_Env = nullptr;
    T m_Ref = nullptr;
};
