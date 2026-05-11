# 📦 Alt Manager - Guia de Instalação para OpenCommunity

## 📋 Sobre

Este Alt Manager permite trocar o nickname in-game sem reiniciar o Minecraft. Funciona com:
- ✅ Minecraft 1.8.9 Vanilla
- ✅ Lunar Client
- ✅ Badlion Client
- ✅ Outros clientes modificados

**Desenvolvido por**: [Seu Nome/Nick]  
**Versão**: 1.0  
**Data**: Maio 2026  

---

## 🚀 Instalação no OpenCommunity

### Passo 1: Adicionar os Arquivos

1. **Copie os arquivos para o projeto**:
   ```
   NickChanger.h  → runtime/src/features/account/NickChanger.h
   NickChanger.cpp → runtime/src/features/account/NickChanger.cpp
   ```

2. **Crie a pasta `account` se não existir**:
   ```
   runtime/src/features/account/
   ```

---

### Passo 2: Registrar o Módulo

**Arquivo**: `runtime/src/features/ModuleRegistry.h`

Adicione o include:
```cpp
#include "account/NickChanger.h"
```

Adicione o registro no método `RegisterAll()`:
```cpp
void ModuleRegistry::RegisterAll() {
    // ... outros módulos ...
    
    // Account
    ModuleManager::Get()->RegisterModule(new NickChanger());
    
    // ... resto do código ...
}
```

---

### Passo 3: Adicionar ao Projeto (Visual Studio)

**Arquivo**: `runtime/runtime.vcxproj`

Adicione nas seções apropriadas:

```xml
<ItemGroup>
  <!-- Outros arquivos .cpp -->
  <ClCompile Include="src\features\account\NickChanger.cpp" />
</ItemGroup>

<ItemGroup>
  <!-- Outros arquivos .h -->
  <ClInclude Include="src\features\account\NickChanger.h" />
</ItemGroup>
```

---

### Passo 4: Adicionar UI no Launcher (Opcional)

Se você quiser adicionar uma interface gráfica no launcher:

**Arquivo**: `launcher/src/ui/Screen.cpp`

Adicione uma nova aba "Account" com:
- Campo de texto para o nick
- Botão "Apply Nick"
- Botão "Open Log File"

**Exemplo de código**:
```cpp
// Na função que renderiza as abas
if (currentTab == 4) { // Aba Account
    ImGui::Text("Alt Manager");
    
    static char nickBuffer[17] = "";
    ImGui::InputText("Nickname", nickBuffer, 17);
    
    if (ImGui::Button("Apply Nick")) {
        // Escrever nick no arquivo temporário
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string nickFile = std::string(tempPath) + "opencommunity_nick.txt";
        
        std::ofstream file(nickFile);
        if (file.is_open()) {
            file << nickBuffer;
            file.close();
        }
    }
    
    if (ImGui::Button("Open Log File")) {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logFile = std::string(tempPath) + "opencommunity_nickchanger.log";
        ShellExecuteA(NULL, "open", logFile.c_str(), NULL, NULL, SW_SHOW);
    }
}
```

---

### Passo 5: Compilar

1. **Abra o projeto no Visual Studio**
2. **Selecione configuração**: Release x64
3. **Compile**: Build → Build Solution (Ctrl+Shift+B)

Se tudo estiver correto, você verá:
```
Build succeeded.
0 Error(s)
```

---

## 📝 Como Usar

### Método 1: Via Launcher (se implementou a UI)

1. Injete a DLL no Minecraft
2. Abra o launcher
3. Vá na aba "Account"
4. Digite o nick desejado (3-16 caracteres)
5. Clique em "Apply Nick"
6. Aguarde 1 segundo
7. Verifique no jogo (TAB ou inventário)

### Método 2: Via Arquivo Manual

1. Injete a DLL no Minecraft
2. Entre em um mundo/servidor
3. Crie um arquivo: `C:\Users\{user}\AppData\Local\Temp\opencommunity_nick.txt`
4. Escreva o nick desejado no arquivo
5. Salve e feche
6. Aguarde 1 segundo
7. O módulo lerá e aplicará o nick automaticamente

---

## 🔧 Configuração do Mapper

O Alt Manager usa o sistema de Mapper para encontrar campos obfuscados. Certifique-se de que seu `Mapper` tenha os seguintes mapeamentos:

**Arquivo**: `runtime/src/game/mapping/Mapper.cpp` (ou similar)

```cpp
// Minecraft class
{ "session", "f" },  // Campo session no Minecraft

// Session class
{ "username", "c" }, // Campo username no Session
```

**Nota**: Os nomes obfuscados (`"f"`, `"c"`) variam por versão/cliente. Ajuste conforme necessário.

---

## 📊 Logs

O Alt Manager gera logs detalhados em:
```
C:\Users\{user}\AppData\Local\Temp\opencommunity_nickchanger.log
```

**Exemplo de log de sucesso**:
```
[2026-05-11 10:30:45] === NICK CHANGE REQUEST DETECTED ===
[2026-05-11 10:30:45] Nick read from file: 'TestNick123'
[2026-05-11 10:30:45] VALIDATION PASSED: Nick 'TestNick123' is valid
[2026-05-11 10:30:45] >>> SetUsername started for: TestNick123
[2026-05-11 10:30:45] Got Minecraft class
[2026-05-11 10:30:45] Found session field
[2026-05-11 10:30:45] Got session object
[2026-05-11 10:30:45] ✓✓✓ USERNAME SET SUCCESSFULLY! ✓✓✓
[2026-05-11 10:30:45] === NICK CHANGE REQUEST COMPLETED ===
```

---

## ⚠️ Requisitos

- ✅ OpenCommunity client com estrutura de módulos
- ✅ Sistema de Mapper implementado
- ✅ JNI configurado corretamente
- ✅ Minecraft 1.8.9
- ✅ Visual Studio 2019+ ou equivalente

---

## 🐛 Troubleshooting

### Problema: "Módulo não compila"
**Solução**: Verifique se todos os includes estão corretos e se a pasta `account` existe.

### Problema: "Nick não muda"
**Solução**: 
1. Verifique se está em jogo (não no menu)
2. Verifique os logs
3. Ajuste os mapeamentos do Mapper

### Problema: "Erro ao encontrar campo session"
**Solução**: Atualize o Mapper com os nomes corretos para sua versão do Minecraft/cliente.

---

## 📚 Estrutura de Arquivos

```
OpenCommunity/
├── runtime/
│   ├── src/
│   │   ├── features/
│   │   │   ├── account/
│   │   │   │   ├── NickChanger.h      ← Adicionar aqui
│   │   │   │   └── NickChanger.cpp    ← Adicionar aqui
│   │   │   └── ModuleRegistry.h       ← Modificar
│   │   └── game/
│   │       └── mapping/
│   │           └── Mapper.cpp         ← Verificar mapeamentos
│   └── runtime.vcxproj                ← Adicionar arquivos
└── launcher/
    └── src/
        └── ui/
            └── Screen.cpp             ← Adicionar UI (opcional)
```

---

## 🎯 Validação de Nick

O Alt Manager valida automaticamente:
- ✅ Comprimento: 3-16 caracteres
- ✅ Caracteres permitidos: A-Z, a-z, 0-9, _
- ❌ Rejeita: Caracteres especiais, espaços, muito curto/longo

---

## 💡 Dicas

1. **Sempre teste em single player primeiro**
2. **Verifique os logs após cada tentativa**
3. **O nick é apenas local** - outros jogadores veem seu nick original
4. **Funciona melhor em servidores offline/cracked**

---

## 📄 Licença

Este código é fornecido "como está" para uso na comunidade OpenCommunity.
Sinta-se livre para modificar e distribuir.

---

## 🤝 Créditos

**Desenvolvido por**: [Seu Nome/Nick]  
**Para**: Comunidade OpenCommunity  
**Agradecimentos**: Comunidade de modding Minecraft  

---

## 📞 Suporte

Se tiver problemas:
1. Verifique os logs
2. Leia o troubleshooting acima
3. Consulte a documentação do OpenCommunity
4. Peça ajuda na comunidade

---

**Versão**: 1.0  
**Data**: Maio 2026  
**Status**: ✅ Pronto para uso
