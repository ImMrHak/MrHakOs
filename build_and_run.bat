@echo off
REM MrHakOS build and run script for Windows
REM 
REM Note: This script is specifically configured for Windows environments.
REM It uses elf32 format for assembly and i386pe emulation mode for linking
REM to create a flat binary output that works with QEMU.

echo Building MrHakOS...

REM Clean any existing binary files
echo Cleaning old binary files...
if exist bin\*.* del /Q bin\*.*

REM Check if required tools are installed
where nasm >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: NASM is not installed or not in PATH. Please install it first.
    exit /b 1
)

where ld >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: LD is not installed or not in PATH. Please install it first.
    exit /b 1
)

where objcopy >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: objcopy is not installed or not in PATH. Please install binutils.
    exit /b 1
)

where qemu-system-i386 >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: QEMU is not installed or not in PATH. Please install it first.
    exit /b 1
)

REM Create bin directory if it doesn't exist
if not exist bin mkdir bin

REM Compile bootloader
echo Compiling bootloader...
nasm -f bin -o bin\bootloader.bin src\boot\bootloader.asm
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to compile bootloader.
    exit /b 1
)

REM Compile kernel entry
echo Compiling kernel entry...
REM Use elf32 format for Windows compatibility
nasm -f elf32 -o bin\entry.o src\kernel\entry.asm
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to compile kernel entry.
    exit /b 1
)

REM Compile kernel
echo Compiling kernel...
REM Use elf32 format for Windows compatibility
nasm -f elf32 -o bin\kernel.o src\kernel\kernel.asm
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to compile kernel.
    exit /b 1
)

REM Link kernel
echo Linking kernel...
REM First create an ELF file (which Windows ld can handle)
ld -m i386pe -o bin\kernel.elf bin\entry.o bin\kernel.o
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to link kernel. Make sure you have the correct version of ld installed.
    echo The error might be related to the format compatibility. Try reinstalling binutils if needed.
    exit /b 1
)

REM Then convert the ELF to a flat binary using objcopy
echo Converting to binary format...
objcopy -O binary bin\kernel.elf bin\kernel.bin
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to convert kernel to binary format.
    echo Make sure objcopy is installed and in your PATH.
    exit /b 1
)

REM Create OS image
echo Creating OS image...
copy /b bin\bootloader.bin + bin\kernel.bin bin\os-image.bin >nul
if %ERRORLEVEL% neq 0 (
    echo Error: Failed to create OS image.
    exit /b 1
)

echo Build completed successfully!

REM Run OS in QEMU
echo Running MrHakOS in QEMU...
qemu-system-i386 -fda bin\os-image.bin

echo Done!