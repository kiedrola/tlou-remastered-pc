The Last of Us Remastered - PC port for Linux (CUSA00552, update 1.11)

PORTUGUÊS

Este pacote NÃO traz nenhum arquivo do jogo. Você precisa da sua própria cópia descriptografada.

Requisitos: Linux de 64 bits (x86-64) com placa de vídeo com Vulkan, e as bibliotecas da sua
distribuição. No Arch:
    sudo pacman -S sdl3 ffmpeg fmt zydis xxhash libx11 vulkan-icd-loader tcl tk
e um driver Vulkan (mesa-vulkan-drivers, ou o driver proprietário da NVIDIA).
O Python (com Tkinter e Pillow) já vem no pacote, em python/: não precisa instalar Python.

1. Extraia o zip em uma pasta com bastante espaço livre.
2. Inicie o launcher:
       ./python/bin/python3 launcher/tlou_launcher.pyw
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

1. Extract the zip to a folder with plenty of free space.
2. Start the launcher:
       ./python/bin/python3 launcher/tlou_launcher.pyw
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
