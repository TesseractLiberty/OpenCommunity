#pragma once

#include "../../../../shared/common/modules/Module.h"
#include "../../../../shared/common/ModuleConfig.h"
#include <string>
#include <fstream>

// Forward declarations para JNI
struct _jclass;
typedef _jclass* jclass;
struct _jobject;
typedef _jobject* jobject;
struct JNIEnv_;
typedef JNIEnv_ JNIEnv;

class NickChanger : public Module {
public:
    MODULE_INFO(NickChanger, "Nick Changer", "Changes player nickname via Alt Manager.", ModuleCategory::Account) {
    }

    std::string GetTag() const override {
        return "";
    }

    void SyncToConfig(void* configPtr) override {}
    void SyncFromConfig(void* configPtr) override {}

#ifdef _RUNTIME
    bool IsSynchronous() const override { return true; }
    void TickSynchronous(void* envPtr) override;

private:
    void SetUsername(void* env, const std::string& username);
    bool IsValidNick(const std::string& nick) const;
    void LogToFile(const std::string& message) const;
    std::string GetLogFilePath() const;
    
    // Contador para polling
    int m_TickCounter = 0;
    
    // Controle de logs
    bool m_LogsEnabled = true;
#endif
};
