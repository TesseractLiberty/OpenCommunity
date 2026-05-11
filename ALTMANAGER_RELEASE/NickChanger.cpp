#include "pch.h"
#include "NickChanger.h"
#include <fstream>
#include <shlobj.h>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

#ifdef _RUNTIME
#include "../../game/classes/Minecraft.h"
#include "../../game/mapping/Mapper.h"
#include "../../game/jni/GameInstance.h"
#include <jni.h>
#endif

#ifdef _RUNTIME

std::string NickChanger::GetLogFilePath() const {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    return std::string(tempPath) + "opencommunity_nickchanger.log";
}

void NickChanger::LogToFile(const std::string& message) const {
    if (!m_LogsEnabled) {
        return;
    }
    
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    
    std::ostringstream timestamp;
    timestamp << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    std::ofstream logFile(GetLogFilePath(), std::ios::app);
    if (logFile.is_open()) {
        logFile << "[" << timestamp.str() << "] " << message << std::endl;
        logFile.close();
    }
    
    printf("[NickChanger] %s\n", message.c_str());
}

bool NickChanger::IsValidNick(const std::string& nick) const {
    if (nick.length() < 3 || nick.length() > 16) {
        LogToFile("VALIDATION FAILED: Nick length " + std::to_string(nick.length()) + " (must be 3-16)");
        return false;
    }
    
    for (char c : nick) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            std::string msg = "VALIDATION FAILED: Invalid character '";
            msg += c;
            msg += "' in nick";
            LogToFile(msg);
            return false;
        }
    }
    
    LogToFile("VALIDATION PASSED: Nick '" + nick + "' is valid");
    return true;
}

void NickChanger::TickSynchronous(void* envPtr) {
    auto* env = static_cast<JNIEnv*>(envPtr);
    
    if (!env) {
        return;
    }
    
    // Poll a cada 20 ticks (~1 segundo)
    m_TickCounter++;
    if (m_TickCounter < 20) {
        return;
    }
    m_TickCounter = 0;
    
    if (!g_Game || !g_Game->IsInitialized()) {
        return;
    }
    
    // Verificar se está em jogo (mundo carregado)
    jobject theWorld = Minecraft::GetTheWorld(env);
    if (!theWorld) {
        return;
    }
    env->DeleteLocalRef(theWorld);
    
    // Verificar se existe arquivo de nick
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string nickFile = std::string(tempPath) + "opencommunity_nick.txt";
    
    std::ifstream file(nickFile);
    if (!file.is_open()) {
        return;
    }
    
    std::string nick;
    std::getline(file, nick);
    file.close();
    
    LogToFile("=== NICK CHANGE REQUEST DETECTED ===");
    LogToFile("File found: " + nickFile);
    LogToFile("Nick read from file: '" + nick + "'");
    
    bool shouldDelete = true;
    
    if (!nick.empty()) {
        if (IsValidNick(nick)) {
            LogToFile("Attempting to apply nick: " + nick);
            SetUsername(env, nick);
        } else {
            LogToFile("ERROR: Nick rejected by validation");
        }
    } else {
        LogToFile("ERROR: Empty nick in file");
    }
    
    if (shouldDelete) {
        if (DeleteFileA(nickFile.c_str())) {
            LogToFile("File deleted successfully");
        } else {
            DWORD error = GetLastError();
            LogToFile("ERROR: Failed to delete file (error code: " + std::to_string(error) + ")");
        }
    }
    
    LogToFile("=== NICK CHANGE REQUEST COMPLETED ===\n");
}

void NickChanger::SetUsername(void* env, const std::string& username) {
    JNIEnv* jniEnv = static_cast<JNIEnv*>(env);
    if (!jniEnv) {
        LogToFile("ERROR: jniEnv is null");
        return;
    }
    
    LogToFile(">>> SetUsername started for: " + username);
    
    jobject mcInst = Minecraft::GetTheMinecraft(jniEnv);
    if (!mcInst) {
        LogToFile("ERROR: Failed to get Minecraft instance");
        return;
    }
    
    jclass mcClass = jniEnv->GetObjectClass(mcInst);
    if (!mcClass) {
        LogToFile("ERROR: Failed to get Minecraft class");
        jniEnv->DeleteLocalRef(mcInst);
        return;
    }
    
    LogToFile("Got Minecraft class");
    
    // Usar Mapper para pegar o nome obfuscado do campo session
    const std::string mappedSessionName = Mapper::Get("session");
    LogToFile("Mapped session name: '" + mappedSessionName + "'");
    
    jfieldID sessionField = nullptr;
    
    if (!mappedSessionName.empty()) {
        // Tentar com tipo avm (Session do Badlion/Lunar)
        sessionField = jniEnv->GetFieldID(mcClass, mappedSessionName.c_str(), "Lavm;");
        if (jniEnv->ExceptionCheck()) {
            LogToFile("Exception checking mapped session field (avm)");
            jniEnv->ExceptionClear();
        } else if (sessionField) {
            LogToFile("SUCCESS: Found session field using mapped name (avm)!");
        }
    }
    
    // Tentar vanilla como fallback
    if (!sessionField) {
        LogToFile("Trying vanilla session field");
        sessionField = jniEnv->GetFieldID(mcClass, "session", "Lnet/minecraft/util/Session;");
        if (jniEnv->ExceptionCheck()) {
            LogToFile("Exception checking vanilla session field");
            jniEnv->ExceptionClear();
        }
    }
    
    if (!sessionField) {
        LogToFile("ERROR: Failed to find session field");
        jniEnv->DeleteLocalRef(mcClass);
        jniEnv->DeleteLocalRef(mcInst);
        return;
    }
    
    LogToFile("Found session field");
    
    jobject session = jniEnv->GetObjectField(mcInst, sessionField);
    if (!session || jniEnv->ExceptionCheck()) {
        LogToFile("ERROR: Failed to get session object");
        jniEnv->ExceptionClear();
        jniEnv->DeleteLocalRef(mcClass);
        jniEnv->DeleteLocalRef(mcInst);
        return;
    }
    
    LogToFile("Got session object");
    
    jclass sessionClass = jniEnv->GetObjectClass(session);
    if (!sessionClass) {
        LogToFile("ERROR: Failed to get session class");
        jniEnv->DeleteLocalRef(session);
        jniEnv->DeleteLocalRef(mcClass);
        jniEnv->DeleteLocalRef(mcInst);
        return;
    }
    
    LogToFile("Got session class");
    
    // Usar Mapper para pegar o nome obfuscado do campo username
    const std::string mappedUsernameName = Mapper::Get("username");
    LogToFile("Mapped username name: '" + mappedUsernameName + "'");
    
    jfieldID usernameField = nullptr;
    
    if (!mappedUsernameName.empty()) {
        usernameField = jniEnv->GetFieldID(sessionClass, mappedUsernameName.c_str(), "Ljava/lang/String;");
        if (jniEnv->ExceptionCheck()) {
            LogToFile("Exception checking mapped username field");
            jniEnv->ExceptionClear();
        } else if (usernameField) {
            LogToFile("SUCCESS: Found username field using mapped name!");
        }
    }
    
    // Tentar vanilla como fallback
    if (!usernameField) {
        LogToFile("Trying vanilla username field");
        usernameField = jniEnv->GetFieldID(sessionClass, "username", "Ljava/lang/String;");
        if (jniEnv->ExceptionCheck()) {
            LogToFile("Exception checking vanilla username field");
            jniEnv->ExceptionClear();
        }
    }
    
    if (usernameField) {
        // Pegar o username atual
        jobject currentUsername = jniEnv->GetObjectField(session, usernameField);
        if (currentUsername) {
            const char* chars = jniEnv->GetStringUTFChars((jstring)currentUsername, nullptr);
            if (chars) {
                LogToFile("Current username: '" + std::string(chars) + "'");
                jniEnv->ReleaseStringUTFChars((jstring)currentUsername, chars);
            }
            jniEnv->DeleteLocalRef(currentUsername);
        }
        
        // Setar o novo username
        LogToFile("Setting new username to: '" + username + "'");
        jstring usernameStr = jniEnv->NewStringUTF(username.c_str());
        jniEnv->SetObjectField(session, usernameField, usernameStr);
        jniEnv->DeleteLocalRef(usernameStr);
        
        // Verificar se foi setado
        jobject newUsername = jniEnv->GetObjectField(session, usernameField);
        if (newUsername) {
            const char* chars = jniEnv->GetStringUTFChars((jstring)newUsername, nullptr);
            if (chars) {
                LogToFile("Verified new username: '" + std::string(chars) + "'");
                jniEnv->ReleaseStringUTFChars((jstring)newUsername, chars);
            }
            jniEnv->DeleteLocalRef(newUsername);
        }
        
        LogToFile("✓✓✓ USERNAME SET SUCCESSFULLY! ✓✓✓");
    } else {
        LogToFile("ERROR: Failed to find username field");
    }
    
    jniEnv->DeleteLocalRef(sessionClass);
    jniEnv->DeleteLocalRef(session);
    jniEnv->DeleteLocalRef(mcClass);
    jniEnv->DeleteLocalRef(mcInst);
    
    LogToFile("<<< SetUsername completed\n");
}

#endif
