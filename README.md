# Ajuste de vídeo

Aplicativo gráfico de ajustes globais de imagem para Linux. Oferece brilho, contraste, gamma, saturação, matiz e temperatura de cor com limites seguros.

## Ambientes compatíveis

- **KDE Plasma/Wayland:** todos os controles são aplicados pelo efeito do KWin, inclusive em janelas de aplicativos em tela cheia. O instalador `.sh` compila a interface e ativa o efeito JavaScript nos dados do usuário. Conteúdo protegido por DRM pode não receber o efeito.
- **GNOME:** todos os controles são aplicados por uma extensão Clutter/Cogl incluída. Use **Instalar suporte** no aplicativo e reabra a sessão.
- **Cinnamon:** todos os controles são aplicados por uma extensão Clutter/Cogl incluída. Use **Instalar suporte** e reabra a sessão.
- **XFCE/X11:** brilho e gamma são aplicados por `xrandr` e duram até a sessão terminar. O XFCE/Wayland não tem backend de compositor implementado.

Os valores confirmados ficam em `~/.config/ajuste-video/settings.json` e na configuração do KWin. O processamento é por saída gráfica; conteúdo protegido/overlay de hardware pode não receber filtros do compositor.

## Instalação no CachyOS

Clone o repositório e execute o instalador:

```bash
git clone https://github.com/xoykor/ajuste-video.git
cd ajuste-video
./install-cachyos.sh
```

O instalador instala as dependências de compilação que faltarem pelo `pacman`, compila a interface em `~/.local` e prepara o efeito em `~/.local/share/kwin/effects/`. No KDE, encerre e reabra a sessão uma vez após instalar para o KWin carregar o efeito. A senha de administrador é usada somente para instalar dependências do sistema; o aplicativo e o efeito ficam nos dados do usuário.

Também é possível executar o instalador da [release mais recente](https://github.com/xoykor/ajuste-video/releases/latest) diretamente:

```bash
curl -fsSL https://github.com/xoykor/ajuste-video/releases/latest/download/install-cachyos.sh | bash
```

Ele baixa o código-fonte da mesma versão antes de compilar. Para remover o aplicativo e os backends instalados, execute:

```bash
curl -fsSL https://github.com/xoykor/ajuste-video/releases/latest/download/uninstall-cachyos.sh | bash
```

O desinstalador não usa `sudo` e preserva seus ajustes em `~/.config/ajuste-video/`.

## Controles

- Brilho: −20% a +20%
- Contraste: 80% a 120% (100% é neutro)
- Gamma: 0,80 a 1,20 (1,00 é neutro)
- Saturação: 75% a 125% (100% é neutro)
- Matiz: −30° a +30°
- Temperatura de cor: −25% a +25%

O efeito também limita esses valores internamente, inclusive se `kwinrc` for editado manualmente.

Os controles e predefinições mostram uma prévia ao vivo. Use **Salvar ajustes** e confirme para manter o perfil; **Descartar prévia** ou fechar a janela restaura o último perfil salvo. Ao reiniciar sem salvar, a prévia expira e o último perfil confirmado volta a ser aplicado. **Restaurar padrão** também é uma prévia até ser salva.

## Compilar manualmente

```bash
cmake -S . -B build -DBUILD_KWIN_EFFECT=OFF -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
cmake --install build
```

São necessárias as dependências de desenvolvimento do Qt 6 para compilar a interface. O processamento global acontece dentro do KWin e requer Plasma 6 com composição OpenGL.

## Licença

GPL-3.0-or-later.
