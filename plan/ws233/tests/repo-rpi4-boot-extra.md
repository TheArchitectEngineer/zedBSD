## Device tree source

`bcm2711-rpi-4-b.dtb` and `overlays/disable-bt.dtbo` are GPL-2.0. Their
complete source is in `source/`, with the paths it has in
[raspberrypi/linux](https://github.com/raspberrypi/linux) at commit
`89586905b8603e545cce9089a81f5f35d65bc998` (the commit the firmware commit's
`extra/git_hash` names): the two `.dts` files, every `.dtsi` they include, and
the `include/dt-bindings/` headers those include.

- `source/arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts` (the 64-bit
  `source/arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b.dts` includes it)
- `source/arch/arm/boot/dts/overlays/disable-bt-overlay.dts`

With the C preprocessor and dtc 1.7.2, these commands make files identical,
byte for byte, to the ones in this directory:

```sh
cd source
cpp -nostdinc -I include -I arch/arm/boot/dts -I arch/arm/boot/dts/broadcom \
    -undef -D__DTS__ -x assembler-with-cpp \
    arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts |
    dtc -@ -q -I dts -O dtb -o bcm2711-rpi-4-b.dtb -
cpp -nostdinc -I include -I arch/arm/boot/dts -undef -D__DTS__ \
    -x assembler-with-cpp arch/arm/boot/dts/overlays/disable-bt-overlay.dts |
    dtc -@ -q -I dts -O dtb -o disable-bt.dtbo -
```

The files under `source/` are covered by `COPYING.linux` (GPL-2.0) and by the
license each file names in its SPDX line.
