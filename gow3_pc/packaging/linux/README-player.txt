The Last of Us Remastered - PC port for Linux (CUSA00552, update 1.11)

PORTUGUÊS

Este pacote NÃO traz nenhum arquivo do jogo. Você precisa da sua própria cópia descriptografada.

Requisitos: Linux de 64 bits (x86-64) com placa de vídeo com Vulkan, e as bibliotecas da sua
distribuição. No Arch:
    sudo pacman -S sdl3 ffmpeg fmt zydis xxhash libx11 vulkan-icd-loader tcl tk
e um driver Vulkan (mesa-vulkan-drivers, ou o driver proprietário da NVIDIA).
O Python (com Tkinter e Pillow) já vem no pacote, em python/: não precisa instalar Python.

Placas de vídeo:
- NVIDIA: precisa do driver do seu sistema (proprietário ou NVK). Testado: GTX 1060 3GB
  (modo de pouca VRAM entra sozinho com 3 GiB ou menos).
- AMD: todas as GPUs com suporte Vulkan na Mesa — Polaris (RX 400/500), Vega, e todo o RDNA
  (RX 5000/6000/7000/9000), CDNA (Instinct) e a GPU da PS5 (RDNA2). A AppImage traz a Mesa
  26.3.1 embutida (não precisa de nada do sistema); o zip usa a mesa-vulkan-drivers do sistema.
- Intel: Haswell (4ª geração) em diante, inclusive as iGPUs atuais e a Arc.
- Mais antigas que isso (AMD pré-Polaris, Intel pré-Haswell): não têm driver Vulkan em
  nenhuma Mesa (nem a do sistema) — não suportadas.
- Em GPU AMD/Intel, a linha "using the bundled Mesa drivers" (AppImage) é o caminho normal,
  não um erro.

1. Extraia o zip em uma pasta com bastante espaço livre.
2. Inicie o launcher:
       ./TLOU-Launcher
   (ou, sem o launcher, com as últimas opções escolhidas nele: ./TLOU-Game)
   Se a extração tirou o bit de execução, devolva-o: `chmod +x TLOU-Launcher TLOU-Game`
3. Clique em "Pastas" e escolha "Arquivos do jogo": a pasta que tem o eboot.bin.
   A atualização 1.11 é procurada ao lado dela, na pasta de mesmo nome com "-patch" no fim
   (por exemplo, CUSA00552 e CUSA00552-patch).
4. Clique em JOGAR. A primeira vez demora mais, porque os shaders são compilados; podem
   aparecer travadas até eles entrarem no cache.

Se o jogo oscilar, teste o limite de 30 FPS no launcher: o movimento fica mais regular.
Saves, configurações e logs ficam em analysis/ dentro desta pasta. Ao abrir, o launcher procura
uma versão nova do port; se houver, o botão ATUALIZAR baixa e instala sem mexer em analysis/.
Ao atualizar à mão, guarde essa pasta. O log é analysis/tlou/boot.log (botão "Abrir log").

ENGLISH

This package has NO game files. You need your own decrypted copy of the game.

Requirements: 64-bit (x86-64) Linux with a Vulkan graphics card and your distribution's
libraries. On Arch:
    sudo pacman -S sdl3 ffmpeg fmt zydis xxhash libx11 vulkan-icd-loader tcl tk
plus a Vulkan driver (mesa-vulkan-drivers, or the NVIDIA proprietary driver).
Python (with Tkinter and Pillow) is included in the package, in python/: no Python install needed.

Graphics cards:
- NVIDIA: your system's driver is required (proprietary or NVK). Tested: GTX 1060 3GB
  (low-VRAM mode kicks in by itself on 3 GiB or less).
- AMD: every GPU with Vulkan support in Mesa — Polaris (RX 400/500), Vega, and all RDNA
  (RX 5000/6000/7000/9000), CDNA (Instinct) and the PS5's GPU (RDNA2). The AppImage ships
  Mesa 26.3.1 bundled (nothing needed from the system); the zip uses the system's
  mesa-vulkan-drivers.
- Intel: Haswell (4th generation) and newer, including current iGPUs and Arc.
- Older than that (pre-Polaris AMD, pre-Haswell Intel): no Vulkan driver exists in any Mesa
  (not even the system's) — unsupported.
- On AMD/Intel GPUs, the "using the bundled Mesa drivers" line (AppImage) is the normal path,
  not an error.

1. Extract the zip to a folder with plenty of free space.
2. Start the launcher:
       ./TLOU-Launcher
   (or, without the launcher, with the last settings chosen in it: ./TLOU-Game)
   If your extractor dropped the executable bit, restore it: `chmod +x TLOU-Launcher TLOU-Game`
3. Click "Folders" and choose "Game files": the folder with eboot.bin. Update 1.11 is looked for
   beside it, in the folder of the same name ending in "-patch" (CUSA00552 and CUSA00552-patch).
4. Click PLAY. The first start takes longer while shaders compile, and you may see stutters until
   they are in the cache.

If the frame rate swings, try the 30 FPS limit in the launcher: motion gets more even.
Saves, settings and logs are in analysis/ inside this folder. When it opens, the launcher looks
for a new version of the port; if there is one, the UPDATE button downloads and installs it
without touching analysis/. Keep that folder when you update by hand.
The log is analysis/tlou/boot.log ("Open log").

Not affiliated with Sony, Naughty Dog or any other rights holder.
GNU GPL v2 or later (LICENSE.txt); the licenses folder lists the bundled libraries and the
distribution libraries the runtime uses.
