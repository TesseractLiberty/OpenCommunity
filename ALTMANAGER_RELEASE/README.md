# 🎮 Alt Manager para OpenCommunity

## 📦 Conteúdo do Pacote

Este pacote contém tudo que você precisa para adicionar o Alt Manager ao OpenCommunity client.

### Arquivos Incluídos:

1. **NickChanger.h** - Header do módulo
2. **NickChanger.cpp** - Implementação do módulo
3. **INSTALLATION_GUIDE.md** - Guia completo de instalação
4. **USER_GUIDE.md** - Guia de uso para usuários finais
5. **README.md** - Este arquivo

---

## ✨ Funcionalidades

- ✅ Troca de nickname in-game sem reiniciar
- ✅ Funciona com Vanilla, Lunar, Badlion
- ✅ Validação automática de nicks
- ✅ Sistema de logs detalhado
- ✅ Fácil integração no OpenCommunity

---

## 🚀 Início Rápido

### Para Desenvolvedores (Adicionar ao OpenCommunity):

1. Leia o **INSTALLATION_GUIDE.md**
2. Copie os arquivos para o projeto
3. Registre o módulo
4. Compile
5. Teste

### Para Usuários Finais:

1. Leia o **USER_GUIDE.md**
2. Injete a DLL
3. Entre em um mundo
4. Crie o arquivo de nick
5. Pronto!

---

## 📋 Requisitos

- OpenCommunity client
- Minecraft 1.8.9
- Visual Studio 2019+ (para compilar)
- Windows 10/11

---

## 🎯 Como Funciona

1. Usuário cria arquivo: `opencommunity_nick.txt`
2. Módulo lê o arquivo a cada 1 segundo
3. Valida o nick (3-16 chars, alphanumeric + _)
4. Aplica o nick via JNI
5. Deleta o arquivo
6. Gera logs detalhados

---

## 📊 Arquivos Temporários

### Arquivo de Nick (Input):
```
Localização: C:\Users\{user}\AppData\Local\Temp\opencommunity_nick.txt
Conteúdo: Apenas o nick (ex: "TestNick123")
Ciclo: Criado → Lido → Deletado
```

### Arquivo de Log (Output):
```
Localização: C:\Users\{user}\AppData\Local\Temp\opencommunity_nickchanger.log
Conteúdo: Logs detalhados de todas as operações
Formato: [YYYY-MM-DD HH:MM:SS] Mensagem
```

---

## 🔧 Tecnologias Usadas

- **C++20** - Linguagem principal
- **JNI** - Interação com Minecraft
- **Windows API** - Manipulação de arquivos
- **Mapper System** - Resolução de nomes obfuscados

---

## ⚠️ Limitações

- Nick é apenas local (cosmético)
- Outros jogadores veem seu nick original
- Não funciona no menu (precisa estar em jogo)
- Alguns servidores podem forçar o nick original

---

## 🐛 Troubleshooting

### Nick não muda?
1. Verifique se está em jogo (não no menu)
2. Verifique os logs
3. Verifique se o nick é válido (3-16 chars)

### Erro ao compilar?
1. Verifique se todos os arquivos foram copiados
2. Verifique os includes
3. Verifique o ModuleRegistry

### Campo não encontrado?
1. Atualize o Mapper com os nomes corretos
2. Verifique a versão do Minecraft/cliente

---

## 📚 Documentação

- **INSTALLATION_GUIDE.md** - Guia completo de instalação para devs
- **USER_GUIDE.md** - Guia de uso para usuários finais

---

## 🤝 Contribuindo

Este código é open source para a comunidade OpenCommunity.

Sinta-se livre para:
- ✅ Modificar o código
- ✅ Adicionar features
- ✅ Corrigir bugs
- ✅ Compartilhar melhorias

---

## 📄 Licença

Fornecido "como está" para uso livre na comunidade.

---

## 🎉 Créditos

**Desenvolvido para**: Comunidade OpenCommunity  
**Versão**: 1.0  
**Data**: Maio 2026  

---

## 📞 Suporte

Para suporte:
1. Leia a documentação
2. Verifique os logs
3. Consulte a comunidade OpenCommunity

---

**Boa sorte e bom uso! 🚀**
