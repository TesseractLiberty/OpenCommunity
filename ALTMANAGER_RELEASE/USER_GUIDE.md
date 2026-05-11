# 📖 Alt Manager - Guia do Usuário

## 🎯 O que é o Alt Manager?

O Alt Manager permite que você **troque seu nickname no Minecraft sem reiniciar o jogo**. É útil para:
- Testar diferentes nicks
- Jogar com nicks personalizados
- Trocar de identidade rapidamente

---

## ⚠️ Importante Saber

- ✅ **Funciona**: Minecraft 1.8.9, Lunar Client, Badlion Client
- ⚠️ **Limitação**: O nick é apenas local (cosmético)
- ⚠️ **Outros jogadores**: Sempre veem seu nick original
- ⚠️ **Servidores**: Alguns podem forçar seu nick original

---

## 🚀 Como Usar (Passo a Passo)

### Passo 1: Injete a DLL

1. Abra o Minecraft 1.8.9
2. Injete a DLL do OpenCommunity
3. Aguarde a injeção completar

### Passo 2: Entre em um Mundo

⚠️ **IMPORTANTE**: O Alt Manager só funciona quando você está **EM JOGO**!

- Entre em um mundo single player **OU**
- Entre em um servidor multiplayer
- **NÃO funciona no menu principal!**

### Passo 3: Crie o Arquivo de Nick

1. Abra o Bloco de Notas (Notepad)
2. Digite o nick desejado (ex: `TestNick123`)
3. Salve o arquivo como:
   ```
   C:\Users\{seu_usuario}\AppData\Local\Temp\opencommunity_nick.txt
   ```

**Atalho rápido**:
1. Pressione `Win + R`
2. Digite: `%TEMP%`
3. Pressione Enter
4. Crie o arquivo `opencommunity_nick.txt` nesta pasta

### Passo 4: Aguarde

- Aguarde **1 segundo**
- O módulo lerá o arquivo automaticamente
- O arquivo será deletado
- Seu nick será alterado!

### Passo 5: Verifique

Verifique se o nick mudou:
- Pressione **TAB** para ver a lista de jogadores
- Abra o **inventário** (E)
- Seu nick deve aparecer alterado

---

## ✅ Regras de Validação de Nick

O nick deve seguir estas regras:

| Regra | Descrição | Exemplo |
|-------|-----------|---------|
| **Comprimento** | 3-16 caracteres | ✅ `Test` ✅ `TestNick123` ❌ `AB` ❌ `NickMuitoLongoQueNaoFunciona` |
| **Caracteres** | Apenas A-Z, a-z, 0-9, _ | ✅ `Test_123` ❌ `Test@123` ❌ `Test 123` |
| **Sem espaços** | Não pode ter espaços | ✅ `TestNick` ❌ `Test Nick` |
| **Sem especiais** | Sem @, #, $, %, etc | ✅ `Test123` ❌ `Test@123` |

---

## 📊 Exemplos de Nicks

### ✅ Nicks Válidos:
```
Test
TestNick
Test123
Test_123
MyNick
Player_1
CoolGuy123
```

### ❌ Nicks Inválidos:
```
AB              (muito curto - menos de 3 chars)
Test@123        (caractere especial @)
Test 123        (espaço)
Test#Nick       (caractere especial #)
NickMuitoLongoQueNaoFunciona  (muito longo - mais de 16 chars)
```

---

## 📝 Verificando os Logs

Os logs ajudam a entender o que aconteceu:

### Localização do Log:
```
C:\Users\{seu_usuario}\AppData\Local\Temp\opencommunity_nickchanger.log
```

### Como Abrir:
1. Pressione `Win + R`
2. Digite: `%TEMP%\opencommunity_nickchanger.log`
3. Pressione Enter

### Log de Sucesso:
```
[2026-05-11 10:30:45] === NICK CHANGE REQUEST DETECTED ===
[2026-05-11 10:30:45] Nick read from file: 'TestNick123'
[2026-05-11 10:30:45] VALIDATION PASSED: Nick 'TestNick123' is valid
[2026-05-11 10:30:45] ✓✓✓ USERNAME SET SUCCESSFULLY! ✓✓✓
[2026-05-11 10:30:45] === NICK CHANGE REQUEST COMPLETED ===
```

**Significado**: Tudo funcionou! O nick foi alterado com sucesso.

### Log de Erro (Nick Inválido):
```
[2026-05-11 10:31:20] === NICK CHANGE REQUEST DETECTED ===
[2026-05-11 10:31:20] Nick read from file: 'AB'
[2026-05-11 10:31:20] VALIDATION FAILED: Nick length 2 (must be 3-16)
[2026-05-11 10:31:20] ERROR: Nick rejected by validation
```

**Significado**: O nick é muito curto. Use 3-16 caracteres.

---

## 🐛 Problemas Comuns

### Problema 1: "Nick não mudou"

**Possíveis causas**:
- Você está no menu (não em jogo)
- O arquivo não foi criado corretamente
- O nick é inválido

**Soluções**:
1. ✅ Entre em um mundo/servidor
2. ✅ Verifique se o arquivo está em `%TEMP%\opencommunity_nick.txt`
3. ✅ Verifique se o nick tem 3-16 caracteres
4. ✅ Verifique os logs

---

### Problema 2: "Arquivo não é deletado"

**Possíveis causas**:
- O módulo não está rodando
- Você não está em jogo
- Erro de permissões

**Soluções**:
1. ✅ Entre em um mundo/servidor
2. ✅ Aguarde 1-2 segundos
3. ✅ Verifique os logs
4. ✅ Delete o arquivo manualmente e tente novamente

---

### Problema 3: "Nick volta ao original"

**Possíveis causas**:
- O servidor está forçando o nick original
- Você saiu e entrou novamente

**Soluções**:
- ⚠️ Isso é esperado em alguns servidores
- ⚠️ O Alt Manager é apenas local (cosmético)
- ⚠️ Aplique o nick novamente após entrar no servidor

---

### Problema 4: "Logs não aparecem"

**Possíveis causas**:
- O módulo não está ativo
- Você não está em jogo
- O arquivo não foi criado

**Soluções**:
1. ✅ Verifique se a DLL foi injetada
2. ✅ Entre em um mundo/servidor
3. ✅ Crie o arquivo de nick novamente

---

## 💡 Dicas e Truques

### Dica 1: Criar um Script
Crie um arquivo `.bat` para facilitar:

```batch
@echo off
set /p nick="Digite o nick: "
echo %nick% > %TEMP%\opencommunity_nick.txt
echo Nick aplicado! Aguarde 1 segundo...
timeout /t 1 >nul
```

Salve como `apply_nick.bat` e execute quando quiser trocar o nick.

### Dica 2: Atalho de Teclado
Use um programa como AutoHotkey para criar um atalho:

```ahk
^!n::  ; Ctrl+Alt+N
InputBox, nick, Alt Manager, Digite o nick:
FileAppend, %nick%, %A_Temp%\opencommunity_nick.txt
MsgBox, Nick aplicado!
return
```

### Dica 3: Verificar Rapidamente
Para ver os logs rapidamente:
1. Pressione `Win + R`
2. Digite: `notepad %TEMP%\opencommunity_nickchanger.log`
3. Pressione Enter

---

## 📋 Checklist de Uso

Antes de reportar problemas, verifique:

- [ ] DLL injetada com sucesso
- [ ] Entrou em um mundo/servidor (não está no menu)
- [ ] Arquivo criado em `%TEMP%\opencommunity_nick.txt`
- [ ] Nick tem 3-16 caracteres
- [ ] Nick usa apenas A-Z, a-z, 0-9, _
- [ ] Aguardou 1-2 segundos
- [ ] Verificou os logs

---

## 🎮 Exemplo Completo

### Cenário: Trocar para o nick "CoolGuy123"

1. **Abra o Minecraft** e injete a DLL
2. **Entre em um mundo** single player
3. **Pressione Win + R** e digite `%TEMP%`
4. **Crie um arquivo** chamado `opencommunity_nick.txt`
5. **Escreva** `CoolGuy123` no arquivo
6. **Salve e feche** o arquivo
7. **Aguarde 1 segundo**
8. **Pressione TAB** no Minecraft
9. **Verifique** se seu nick mudou para "CoolGuy123"
10. **Abra os logs** para confirmar: `%TEMP%\opencommunity_nickchanger.log`

---

## 📞 Suporte

Se tiver problemas:
1. ✅ Leia este guia completamente
2. ✅ Verifique os logs
3. ✅ Siga o troubleshooting acima
4. ✅ Consulte a comunidade OpenCommunity

---

## 🎉 Conclusão

O Alt Manager é uma ferramenta simples mas poderosa para trocar seu nick in-game. Lembre-se:

- ✅ Sempre entre em jogo antes de aplicar
- ✅ Use nicks válidos (3-16 chars)
- ✅ Verifique os logs em caso de problemas
- ⚠️ O nick é apenas local (cosmético)

**Divirta-se! 🚀**

---

**Versão**: 1.0  
**Data**: Maio 2026  
**Para**: Comunidade OpenCommunity
