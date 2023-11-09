#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

#Echo build step.
#execute or skip or exit
buildstep()
{
  echo -e "--Step:  $1 \n"
  if test $STEPACTION = "ask"
  then
    read -p ">>>>How do you want to proceed (y/s/n)?  " answer
    case ${answer:0:1} in
      y|Y )
          true
      ;;
      s|S )
          echo -e "\tSkipping\n"
          return
      ;;
      * )
          echo -e "\tExiting now\n"
          exit
      ;;
    esac
  fi

  #execute the step
  echo -e "\tRunning\n"
  $1
}

#Echo substep or exit; no skipping allowed on substeps
buildsubstep()
{
  echo -e "----SubStep:  $1 \n"
  if test $STEPACTION = "ask"
  then
    read -p ">>>>>>How do you want to proceed (y/n)?  " answer
    case ${answer:0:1} in
      y|Y )
          true
      ;;
      * )
          echo -e "\tExiting now"
          exit
      ;;
    esac
  fi
}


OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
  echo -e "Using default directory ${OUTDIR} for output \n"
else
  OUTDIR=$1
  echo -e "Using passed directory ${OUTDIR} for output \n"
fi

mkdir -p ${OUTDIR}

#check if script was called to run without interrupts
#default to ask
STEPACTION="ask"
if [ $# -eq 2 ]
then
  if test $2 == "noask"
  then
    STEPACTION="noask"
  fi
fi

cd "$OUTDIR"

clonelinuxstep()
{
  cd "$OUTDIR"
  buildsubstep "Check and clone linux"
  if [ ! -d "${OUTDIR}/linux-stable" ]; then
      #Clone only if the repository does not exist.
    echo -e "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR} \n"
    set -x
    git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
    set +x
  fi
}
buildstep clonelinuxstep

buildlinuxstep()
{
  cd "$OUTDIR"
  if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
      cd linux-stable
      buildsubstep "Checking out version ${KERNEL_VERSION}"
      set -x
      git checkout ${KERNEL_VERSION}
      set +x

      # PSK-DONE: Add your kernel build steps here

      #deep clean
      buildsubstep "Build Step 1: Deep Cleaning the kernel workspace"
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
      set +x

      #build defconfig for "virt" arm dev board
      buildsubstep "Build Step 2: Building def config for virt arm dev board"
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
      set +x

      #build vmlinux target
      buildsubstep "Build Step 3: Build kernel image for virt arm dev"
      set -x
      make -j2 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
      set +x

      #build modules
      buildsubstep "Build Step 4: Build modules"
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules
      set +x

      #build device tree
      buildsubstep "Build Step 4: Build the devicetree"
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs
      set +x
  fi
}
buildstep buildlinuxstep


rootfsstagingstep()
{
  cd "$OUTDIR"
  echo -e "Adding the Image in outdir"
  buildsubstep "Creating the staging directory for the root filesystem"
  if [ -d "${OUTDIR}/rootfs" ]
  then
      buildsubstep "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
      set -x
      sudo rm  -rf ${OUTDIR}/rootfs
      set +x
  fi

  # PSK-DONE: Create necessary base directories
  set -x
  mkdir -p ${OUTDIR}/rootfs
  cd ${OUTDIR}/rootfs
  mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
  mkdir -p usr/bin usr/lib usr/sbin
  mkdir -p var/log
  set +x
  echo -e "-------------staged dirs------------------------ \n"
  ls ${OUTDIR}/rootfs/*
  echo -e "------------------------------------------------ \n"
}
buildstep rootfsstagingstep

busyboxstep()
{
  cd "$OUTDIR"
  buildsubstep "Cloning busybox"
  if [ ! -d "${OUTDIR}/busybox" ]
  then
      set -x
      git clone git://busybox.net/busybox.git
      cd busybox
      git checkout ${BUSYBOX_VERSION}
      set +x
      # TODO:  Configure busybox
  else
      cd busybox
  fi

  # PSK-DONE: Make and install busybox
  buildsubstep "Busybox build step1: clean"
      set -x
  make distclean
      set +x
  buildsubstep "Busybox build step2: defconfig"
      set -x
  make defconfig
      set +x
  buildsubstep "Busybox build step3: cross compile"
      set -x
  make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
      set +x
  buildsubstep "Busybox build step4: install"
      set -x
  make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
      set +x
}
buildstep busyboxstep

libdependencysteps()
{
  echo -e "Library dependencies"
  #pidependency=${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
  #sharedlibdependency=${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"
  #pild=echo -e $pidependency |sed 's/^.*lib\///' |sed 's/\].*$//' "\n
  ${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
  ${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"
  CROSS_COMPILE_ROOT=dirname ${CROSS_COMPILE}gcc

  # PSK-DONE: Add library dependencies to rootfs
  buildsubstep "Copying library dependencies to rootfs"
      set -x
  cd $CROSS_COMPILE_ROOT
  cp ../lib64/libm.so.6 ${OUTDIR}/rootfs/lib64
  cp ../lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64
  cp ../lib64/libc.so.6 ${OUTDIR}/rootfs/lib64
  cp ../lib64/ld-linux-aarch.so.1 ${OUTDIR}/rootfs/lib64
      set +x
}
buildstep libdependecysteps

# PSK-DONE: Make device nodes
makenodesteps()
{
  buildsubstep "Making device nodes"
      set -x
  cd ${OUTDIR}/rootfs
  mknod -m 666 /dev/null c 1 3
  mknod -m 666 /dev/console c 5 1
      set +x
}
buildstep makenodesteps

echo -e "----staged rootfs with busybox,deps and devices------------------- \n"
ls ${OUTDIR}/rootfs/*/*
echo -e "------------------------------------------------------------------ \n"

# PSK-DONE: Clean and build the writer utility
buildfinderappsteps()
{
  buildsubstep "Building the finder app..."
      set -x
  cd ${FINDER_APP_DIR}
  make clean
  make CROSS_COMPILE=${CROSS_COMPILE}
      set +x
}
buildstep buildfinderappsteps


copyfinderappstep()
{
  # PSK-DONE: Chown the root directory
  buildsubstep "Changing rootfs ownership to root "
      set -x
  find ${OUTDIR}/rootfs -exec chown root:root {}
      set +x
}
buildstep copyfinderappstep

createinitramfsstep()
{
  # PSK-DONE: Create initramfs.cpio.gz
  buildsubstep "Changing rootfs ownership to root "
      set -x
  cd "$OUTDIR/rootfs"
  find . |cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
  gzip -f initramfs.cpio
      set +x
}
buildstep createinitramfsstep


