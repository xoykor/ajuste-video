# Ajuste de vídeo

Aplicativo gráfico de ajustes globais de imagem para Linux. Oferece brilho, contraste, gamma, saturação, matiz e temperatura de cor com limites seguros.

## Ambientes compatíveis

- **KDE Plasma/Wayland:** todos os controles são aplicados pelo efeito do KWin, inclusive em janelas de aplicativos em tela cheia. O AppImage inclui o efeito JavaScript e o ativa na pasta de dados do usuário pelo botão **Ativar / atualizar KWin**. Não chama `sudo`, `pkexec`, `pacman`, nem precisa compilar o plugin na máquina. Conteúdo protegido por DRM pode não receber o efeito.
- **GNOME:** todos os controles são aplicados por uma extensão Clutter/Cogl incluída. Use **Instalar suporte** no aplicativo e reabra a sessão.
- **Cinnamon:** todos os controles são aplicados por uma extensão Clutter/Cogl incluída. Use **Instalar suporte** e reabra a sessão.
- **XFCE/X11:** brilho e gamma são aplicados por `xrandr` e duram até a sessão terminar. O XFCE/Wayland não tem backend de compositor implementado.

GNOME/Cinnamon e o aplicativo leem os ajustes de `~/.config/ajuste-video/settings.json`; assim, os valores persistem e voltam a ser aplicados quando a extensão carrega. No KDE, os valores também são escritos na configuração do KWin. O processamento é por saída gráfica; conteúdo protegido/overlay de hardware pode não receber filtros do compositor.

## Instalação no CachyOS

No terminal, entre nesta pasta e execute:

```bash
./install-cachyos.sh
```

O script compila e instala a interface em `~/.local`. Abra **Ajuste de vídeo** pelo menu e use **Ativar / atualizar KWin** uma vez. O efeito fica em `~/.local/share/kwin/effects/`; não instala pacotes nem grava arquivos em `/usr`.

## AppImage

Tags `v*` no GitHub geram uma release com `ajuste-video-x86_64.AppImage`. O arquivo inclui a interface e todos os backends. No KDE, o AppImage copia e ativa o efeito dentro dos dados do usuário; não requer instalador auxiliar, gerenciador de pacotes, senha de administrador ou compilação no computador.

Para gerar localmente, instale as dependências do build e `linuxdeploy-x86_64.AppImage`, depois execute `packaging/build-appimage.sh` com `LINUXDEPLOY=/caminho/para/linuxdeploy`.

## Controles

- Brilho: −20% a +20%
- Contraste: 80% a 120% (100% é neutro)
- Gamma: 0,80 a 1,20 (1,00 é neutro)
- Saturação: 75% a 125% (100% é neutro)
- Matiz: −30° a +30°
- Temperatura de cor: −25% a +25%

O efeito também limita esses valores internamente, inclusive se `kwinrc` for editado manualmente.

Os ajustes e predefinições são salvos em `kwinrc` e aplicados ao mover os controles. O botão **Restaurar padrão** desativa o efeito e devolve a imagem original.

## Compilar e instalar no usuário atual

```bash
cmake -S . -B build -DBUILD_KWIN_EFFECT=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
cmake --install build
```

São necessárias as dependências de desenvolvimento do Qt 6 para compilar a interface. O processamento global acontece dentro do KWin e requer Plasma 6 com composição OpenGL; o AppImage instala esse efeito apenas nos dados do usuário.

## Licença

GPL-3.0-or-later.
