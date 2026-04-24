import os

TARGET_DIR = "./user/target/bin/"

if __name__ == '__main__':
    f = open("os/kernel_app.ld", mode="w")
    apps = os.listdir(TARGET_DIR)
    f.write(
'''OUTPUT_ARCH(riscv)
ENTRY(_start)
BASE_ADDRESS = 0x80200000;

SECTIONS
{
    . = BASE_ADDRESS;
    skernel = .;

    stext = .;
    .text : {
        *(.text.entry)
        . = ALIGN(4K);
        strampoline = .;
        *(.text.trampoline);
        . = ALIGN(4K);
        *(.text .text.*)
    }

    . = ALIGN(4K);
    etext = .;
    srodata = .;
    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    }

    . = ALIGN(4K);
    erodata = .;
    sdata = .;
    .data : {
        *(.data)
''')
    for (idx, _) in enumerate(apps):
        f.write('        . = ALIGN(0x1000);\n')
        f.write('        *(.data.app{})\n'.format(idx))
    f.write(
'''
        . = ALIGN(0x1000);
        *(.data.*)
        *(.sdata .sdata.*)
    }

    . = ALIGN(4K);
    edata = .;
    sbss_with_stack = .;
    .bss : {
        *(.bss.stack)
        sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
    }

    . = ALIGN(4K);
    ebss = .;
    ekernel = .;

    /DISCARD/ : {
        *(.eh_frame)
    }
}
''')
    f.close()
