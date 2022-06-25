# MalUnpackCompanion driver

[![Commit activity](https://img.shields.io/github/commit-activity/m/hasherezade/mal_unpack_drv)](https://github.com/hasherezade/mal_unpack_drv/commits)
[![Last Commit](https://img.shields.io/github/last-commit/hasherezade/mal_unpack_drv/main)](https://github.com/hasherezade/mal_unpack_drv/commits)

[![GitHub release](https://img.shields.io/github/release/hasherezade/mal_unpack_drv.svg)](https://github.com/hasherezade/mal_unpack_drv/releases)
[![GitHub release date](https://img.shields.io/github/release-date/hasherezade/mal_unpack_drv?color=blue)](https://github.com/hasherezade/mal_unpack_drv/releases)
[![Github All Releases](https://img.shields.io/github/downloads/hasherezade/mal_unpack_drv/total.svg)](https://github.com/hasherezade/mal_unpack_drv/releases)
[![Github Latest Release](https://img.shields.io/github/downloads/hasherezade/mal_unpack_drv/latest/total.svg)](https://github.com/hasherezade/mal_unpack_drv/releases)

[![License](https://img.shields.io/badge/License-BSD%202--Clause-blue.svg)](https://github.com/hasherezade/mal_unpack_drv/blob/main/LICENSE)
[![Platform Badge](https://img.shields.io/badge/Windows-0078D6?logo=windows)](https://github.com/hasherezade/mal_unpack_drv)

MalUnpack companion driver: enhances capabilities of [mal_unpack](https://github.com/hasherezade/mal_unpack), isolates the run sample from the environment.

Works with: https://github.com/hasherezade/mal_unpack


Supported systems: Windows, starting from 7. Recommended system: Windows 10.

**WARNING: This is an experimental version, use it on a Virtual Machine only!**

## How to install


1. The driver is signed by a test signature, so, in order for the installation to succeed, Test Signing must be enabled on the target machine. As an Administrator, deploy the following command:

```
bcdedit /set testsigning on
```

Then reboot the system...

_NOTE: In case if this is not sufficient, try another method (using Advanced Boot Options) described [here](https://www.howtogeek.com/167723/how-to-disable-driver-signature-verification-on-64-bit-windows-8.1-so-that-you-can-install-unsigned-drivers/)._

2. Right click on `MalUnpackCompanion.inf` from the driver package. From the context menu, choose "Install"
3. After the driver is installed, it remains inactive. In order to activate it, run the following command as Administrator:
```
fltmc load MalUnpackCompanion
```

## How to unload

Run the commandline as Administrator. Deploy the command:
```
fltmc unload MalUnpackCompanion
```

##  How to update

1. Unload the driver (check [How to unload](https://github.com/hasherezade/mal_unpack_drv/blob/main/README.md#how-to-unload))
2. Repeat the installation steps 2 to 3 (check [How to install](https://github.com/hasherezade/mal_unpack_drv/blob/main/README.md#how-to-install))

## Confirm that the driver is loaded

#### Option 1.

Run the commandline as Administrator. Deploy the command:
```
fltmc
```
You should see `MalUnpackCompanion` on the list of installed filter drivers.

#### Option 2.

Install Nirsoft DriversList (available [here](https://www.nirsoft.net/utils/installed_drivers_list.html)). Check if `MalUnpackCompanion` is on the list, and if it is running. This tool allows you also to easily check the version of the currently installed driver.

How to use
---

Download the [`mal_unpack`](https://github.com/hasherezade/mal_unpack) userland application, and use it as it is mentioned in the instructions. If the `MalUnpackCompanion` driver is installed and loaded, the userland application will detect it automatically, and communicate with it.

