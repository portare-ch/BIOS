# Portare BIOS

Portare BIOS is the Retroid Pocket Nova's bootloader: Qualcomm's Android
bootloader (ABL) with a boot menu, for PortareOS and Android. It starts
either system, installs PortareOS from an SD card to internal storage, and
shows its own charging screen. It is a fork of OpenABL by the Open Handheld
Collective. The ABL patch and integration files are in `Abl/`; the menu
program is embedded in the ABL's firmware volume.

<p>
  <img alt="The main menu" src="Docs/main-menu.png" width="49%">
  <img alt="The boot list" src="Docs/boot-list.png" width="49%">
</p>

The main menu and the boot list, rendered by the simulator (`make sim`).

## Installing BIOS

Use `abl_signed-SM8550.elf` from the
[releases](https://github.com/portare-ch/BIOS/releases). From fastboot,
with ABL flashing unlocked, write both slots:

```bash
fastboot flash abl_a abl_signed-SM8550.elf
fastboot flash abl_b abl_signed-SM8550.elf
fastboot reboot
```

Fastboot's `product` is `PortareBIOS`. PortareOS bundles BIOS, and its
updates write the bundled image to both slots. The releases also have
`openabl-recovery-flashable-sm8550.zip`, an Android recovery addon that
flashes both slots and installs an `addon.d` script to restore the
opposite slot after compatible OTA updates.

## Using it

At power-on, unless the menu picked a system for this boot:

| Held | What happens |
|---|---|
| Nothing | Starts the first system of the Boot order: PortareOS by default, or Android. A failed start opens the menu with an error. |
| Volume Down | Opens the menu, with fastboot available over USB. `fastboot reboot bootloader` also opens it, and so does a keyboard's Delete key. |
| Volume Up | With PortareOS first, starts Android. With Android first, starts Android's recovery. |

When off-mode charging is on (`fastboot oem off-mode-charge 1`) and a
charger switches the Nova on, BIOS shows its charging screen: the battery
and its charge, estimated from the voltage. The display goes off after 5
seconds and any key shows it again. Power starts the first system of the
Boot order, a volume key opens the menu, and unplugging the charger turns
the Nova off. `fastboot oem off-mode-charge 0` turns it off.

In the menu, Volume Up and Down move the selection and Power selects.
Lists have a **Back** row.

| Item | What it does |
|---|---|
| Boot | Opens the boot list. Selecting a system starts it once, without changing the Boot order. |
| Boot order | Switches between **PortareOS, Android** and **Android, PortareOS**. Shown when Android is installed. |
| Install PortareOS | Copies PortareOS from a PortareOS SD card to internal storage; see below. |
| Emergency download mode | Restarts into Qualcomm EDL mode, after confirmation. |
| Restart | Restarts, starting the first system of the Boot order. |
| Shut down | Powers off. |

The boot list shows PortareOS on internal storage and on the SD card, then
Android. A boot image on a volume not labelled `PORTAREOS` shows as Linux. A choice from it applies once. If its drive is missing or it
doesn't start, the menu says so and nothing else is started.

## Installing PortareOS

Install PortareOS needs a PortareOS SD card: a FAT volume labelled
`PORTAREOS` with `KERNEL` and `SYSTEM`. BIOS copies the card's files
itself; neither PortareOS nor Android runs during the install. It writes
the partition table, formats a 2 GiB boot partition (FAT32 with 4 KiB
sectors, to match internal storage) and copies the files to it, creates
an empty ext4 storage partition, and reads everything back. Each choice
shows the resulting space before it asks to confirm.

- **Dualboot** keeps Android. A first install shrinks Android's data
  partition to the size chosen; Android's data is encrypted, so Android is
  reset. With PortareOS already next to Android, only PortareOS's system
  is replaced, and its games, saves and settings stay.
- **Replace Android** removes Android's system and data, and any
  PortareOS already there, and gives PortareOS all of that space.
  Android's `vbmeta` is rewritten with verification disabled, since the
  `vbmeta_system` it chains is gone.

The install refuses, before writing anything, a missing or unreadable
card, a partition table whose two copies don't agree, a layout it doesn't
know, too little space and a battery too low to flash.

## How the systems start

PortareOS starts from `\KERNEL` on its boot partition, internal storage
first, then the SD card. It is an Android boot image (header v0 to v2),
checked by Qualcomm's verified boot. BIOS lays it out as Qualcomm's
BootLinux does, with the devicetree whose model is `Retroid Pocket Nova`
from those appended to the kernel, and reserves XBL's splash memory in it.
The command line gets `portare.bootpart=PARTUUID=<guid>`, naming the
partition the kernel came from, so the initramfs mounts that installation:
an SD card and an install made from it carry the same labels.

A failed PortareOS start leaves Android's A/B slot state alone. Android
starts from its own boot partitions, through Qualcomm's boot path with
verified boot and recovery.

The Boot order is BIOS's only setting: the UEFI variable `BootMode`, one
byte, nonzero for Android first. The menu's one-shot choices are variables
too. The vendor GUID is in `Efi/Variables.h`.

## Building

Requires git, zip, and a running Docker engine or Apple's `container` on
a Mac. The build uses a `linux/amd64` container, including on Apple Silicon.
`build.sh` prefers Docker when it is running.

```bash
./build.sh sm8550
```

| Output | Contents |
|---|---|
| `build/out/abl_signed-SM8550.elf` | Signed ABL image, with a neighbouring `.sha256` file |
| `build/out/openabl-recovery-flashable-sm8550.zip` | The Android recovery addon |
| `build/abl-build.log` | Full build log |

`--clean` builds from scratch. `VERSION` holds `MAJOR.MINOR`, which the
menu and fastboot's `version-bootloader` report.

`Scripts/fetch.sh` fetches the pinned qtestsign and Qualcomm edk2 sources
into `build/`. The edk2 tag is `KERNEL.PLATFORM.3.0.r23-02400-kernel.0`;
the script applies `Abl/openabl.patch` and copies `Abl/QcomModulePkg` into
it. Later builds reuse sources and build output. To build only
`build/menu/openabl-menu.efi` with local clang, lld-link and make, run
`Scripts/fetch.sh edk2`, then `make`.

The host simulator needs a C compiler and zlib:

```bash
make sim
build/sim/openabl-menu-sim
```

It writes PNGs of every screen to `build/screens`, as the Nova shows them.
Use `--out DIR` or `--filter TEXT` to control output, or `--check` to
check redraw consistency.

`make test` writes FAT32 and ext4 images with the installer's formatters
and checks them with fsck.fat, mtools, e2fsck and mke2fs, and checks the
installer's partition layouts and `vbmeta`. `Scripts/test.sh` runs it in a
container that has those tools.

GitHub Actions builds every pull request to `main` and runs the tests. On
a `v*` tag it uploads two files to the release:
`abl_signed-SM8550.elf` and `openabl-recovery-flashable-sm8550.zip`.
GitHub shows each file's SHA-256. CI caches fetched sources in the
`sources` pre-release, keyed by the pins in `Scripts/fetch.sh`.

## Licence

GPL-3.0-or-later (`LICENSE`), with upstream OpenABL copyright
(C) 2026-present Open Handheld Collective. `Abl/` adds permission to link
with Qualcomm's ABL. `LICENSES.md` explains the exception and lists
third-party components; licence texts are in `LICENSES/`.
