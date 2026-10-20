# Onimusha3Patch

1. Patch Onmimusha 3 to allow any screen resolution.
2. Fix the Configure input menu (see Configure input patch directory).

## How to use
1. Run build.bat
2. Copy dsound.dll, Oni3Patch.dll and Oni3Patch.ini to Onimusha 3 directory.
3. Run Oni3.exe

## How it works
dsound.dll injects Oni3Patch.dll into the game (oni3.exe).

OniPatch.dll hooks DirectX 9 and CreateWindowExW to force the resolution size.

## Screens

Onimusha in 1920x1080
![image](https://user-images.githubusercontent.com/5740369/140754099-15c1f44c-f68f-49d5-8951-a847b1b9cb0c.png)

Configure input
![image](https://user-images.githubusercontent.com/5740369/140755030-882cac2b-89f0-4b14-8c95-096da3573139.png)

## Windows Defender Detection (Win32/DefenceEvasion.A!ml)
That detection is a machine-learning heuristic (the `!ml` suffix), not a signature match. It triggers because the injector does exactly what malware loaders do: `CreateProcess(CREATE_SUSPENDED)`, then `VirtualAllocEx`, `WriteProcessMemory`, and `CreateRemoteThread(LoadLibraryA)`. Windows Defender can’t tell a game patcher from a trojan by that sequence, so it’s a false positive, but a predictable one.

Been thinking if I should use a .dll proxy in the future. (Done)
