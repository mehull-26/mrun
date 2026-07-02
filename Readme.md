# mrun - Minimal Linux Container Runtime

`mrun` is a tiny experimental container runtime written in C.
It can start a process inside a basic Linux container using namespaces, a separate root filesystem, and a mounted `/proc`.

## What it can do

* Run a command inside a given root filesystem
* Create isolated namespaces:

  * PID namespace
  * mount namespace
  * UTS/hostname namespace
  * IPC namespace
  * network namespace
* Switch into the container rootfs using `pivot_root`
* Mount a fresh `/proc`
* Bring up loopback interface `lo`
* Run the requested command as PID 1 inside the container

## Build

```bash
gcc -Wall -Wextra -O2 mrun.c -o mrun
```

## Usage

```bash
sudo ./mrun <rootfs> <command> [args...]
```

Example:

```bash
sudo ./mrun ~/alpine /bin/sh
```

Or, if Alpine is relative to the current directory:

```bash
sudo ./mrun ../alpine /bin/sh
```

## Test inside the container

```bash
hostname
echo $$
mount | grep proc
ip link
```

Expected behavior:

* Hostname should be changed inside the container
* Shell should run as PID 1
* `/proc` should show container-local processes
* Only loopback networking is available by default

## Current limitations

This is Phase 1 only. It does not yet support:

* OCI `config.json`
* cgroups/resource limits
* Docker-style image pulling
* container names/state tracking
* logs
* volumes
* bridge networking
* port forwarding
* seccomp/capability hardening
* rootless containers

This runtime is suitable for learning and trusted personal experiments, not hardened sandboxing.

