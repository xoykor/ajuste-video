# Ajuste de vídeo

Aplicativo gráfico de ajustes globais de imagem para Linux. Oferece brilho, contraste, gamma, saturação, matiz e temperatura de cor com limites seguros.

O repositório indicado (`xoykor/ajuste-video`) não contém arquivos ainda. Esta pasta é a primeira implementação local do projeto.

## Ambientes compatíveis

- **KDE Plasma/Wayland:** todos os controles são aplicados pelo efeito do KWin. O AppImage instala o suporte específico para a versão local do KWin pelo botão **Instalar suporte**; o processo usa `sudo` para instalar as dependências e o plugin no sistema.
- **GNOME:** todos os controles são aplicados por uma extensão Clutter/Cogl incluída. Use **Instalar suporte** no aplicativo e reabra a sessão.
- **Cinnamon:** todos os controles são aplicados por uma extensão Clutter/Cogl incluída. Use **Instalar suporte** e reabra a sessão.
- **XFCE/X11:** brilho e gamma são aplicados por `xrandr` e duram até a sessão terminar. O XFCE/Wayland não tem backend de compositor implementado.

GNOME/Cinnamon e o aplicativo leem os ajustes de `~/.config/ajuste-video/settings.json`; assim, os valores persistem e voltam a ser aplicados quando a extensão carrega. No KDE, os valores também são escritos na configuração do KWin. O processamento é por saída gráfica; conteúdo protegido/overlay de hardware pode não receber filtros do compositor.

## Instalação no CachyOS

No terminal, entre nesta pasta e execute:

```bash
./install-cachyos.sh
```

O instalador compila o efeito do KWin e a interface Qt, instala os arquivos no sistema e habilita o efeito. Depois, encerre a sessão Plasma e entre novamente. Abra **Ajuste de vídeo** pelo menu de aplicativos.

## AppImage

Tags `v*` no GitHub geram uma release com `ajuste-video-x86_64.AppImage`. O AppImage inclui a interface e os backends GNOME/Cinnamon. O efeito do KWin precisa ser instalado no sistema porque os plugins do KWin têm de corresponder à versão do compositor instalado.

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

## Dependências para compilar manualmente

```bash
sudo pacman -S --needed base-devel cmake extra-cmake-modules kwin qt6-base kconfig
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

O processamento acontece no KWin. A sessão precisa estar em Plasma 6 com composição OpenGL.

## Licença

GPL-3.0-or-later.
