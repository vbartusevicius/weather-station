# STM32G431 (Cortex-M4F): build and link with the hardware FPU (the board default links the soft-float libc).
Import("env")  # noqa: F821  (PlatformIO SCons global)

FPU = ["-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"]
env.Append(CCFLAGS=FPU, ASFLAGS=FPU, LINKFLAGS=FPU)  # noqa: F821
