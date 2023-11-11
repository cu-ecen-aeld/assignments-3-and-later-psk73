#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
# Author: Sreekanth Pasumarthy

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

#Echo substep or exit; skip if second param is "skip"
#Use skip for substeps that may take long, like build linux kernle.
buildsubstep()
{
  echo -e "----SubStep:  $1 \n"
  if test $STEPACTION = "ask"
  then
    read -p ">>>>>>How do you want to proceed (y/s/n)?  " answer
    case ${answer:0:1} in
      y|Y )
          true
      ;;
      s|S )
          echo -e "\tSkipping\n"
          return
      ;;
      * )
        if test $2 = "skip"
        then
           echo -e "\tSkipping"
        else
           echo -e "\tExiting now"
           exit
        fi
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
#default to noask, as that is requirement for assignment
STEPACTION="noask"
if [ $# -eq 2 ]
then
  if test $2 == "ask"
  then
    STEPACTION="ask"
  fi
fi

cd "$OUTDIR"

clonelinuxstep()
{
  cd "$OUTDIR"
  buildsubstep "Check and clone linux" "skip"
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
      buildsubstep "Checking out version ${KERNEL_VERSION}" "skip"
      set -x
      git checkout ${KERNEL_VERSION}
      set +x

      # PSK-DONE: Add your kernel build steps here

      #deep clean
      buildsubstep "Build Step 1: Deep Cleaning the kernel workspace" "skip"
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
      set +x

      #build defconfig for "virt" arm dev board
      buildsubstep "Build Step 2: Building def config for virt arm dev board" "skip"
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
      set +x

      #build vmlinux target
      buildsubstep "Build Step 3: Build kernel image for virt arm dev" "skip"
      set -x
      make -j2 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
      set +x

      #build modules
      buildsubstep "Build Step 4: Build modules" "skip"
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules
      set +x

      #build device tree
      buildsubstep "Build Step 4: Build the devicetree" "skip"
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
  buildsubstep "Creating the staging directory for the root filesystem" "skip"
  if [ -d "${OUTDIR}/rootfs" ]
  then
      buildsubstep "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over" "skip"
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
  tree ${OUTDIR}/rootfs/
  echo -e "------------------------------------------------ \n"
}
buildstep rootfsstagingstep

busyboxstep()
{
  cd "$OUTDIR"
  buildsubstep "Cloning busybox" "skip"
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
  buildsubstep "Busybox build step1: clean" "skip"
      set -x
      make distclean
      set +x
  buildsubstep "Busybox build step2: defconfig" "skip"
      set -x
      make defconfig
      set +x
  buildsubstep "Busybox build step3: cross compile" "skip"
      set -x
      make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
      set +x
  buildsubstep "Busybox build step4: install" "skip"
      set -x
      make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
      set +x
}
buildstep busyboxstep

libdependencysteps()
{
  cd ${OUTDIR}/rootfs
  echo -e "Library dependencies"
  ${CROSS_COMPILE}readelf -a ./bin/busybox | grep "program interpreter"
  ${CROSS_COMPILE}readelf -a ./bin/busybox | grep "Shared library"
  #CROSS_COMPILE_ROOT="$(dirname "${CROSS_COMPILE_ROOT}")"
  CROSS_COMPILE_ROOT=/home/psk/coursera/aeld/armcc/arm-gnu-toolchain-13.2.Rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu
  echo "----- ${CROSS_COMPILE_ROOT}----"

  # PSK-DONE: Add library dependencies to rootfs
  buildsubstep "Copying library dependencies to rootfs" "skip"
      set -x
  cd $CROSS_COMPILE_ROOT
  pwd
  sudo cp -f ./libc/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64
  sudo cp -f ./libc/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64
  sudo cp -f ./libc/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64
  sudo cp -f ./libc/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib64
  sudo cp -f ./libc/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib
      set +x
}
buildstep libdependencysteps

# PSK-DONE: Make device nodes
makenodesteps()
{
  buildsubstep "Making device nodes" "skip"
      set -x
  cd ${OUTDIR}/rootfs
  sudo mknod -m 666 ./dev/null c 1 3
  sudo mknod -m 666 ./dev/console c 5 1
      set +x
}
buildstep makenodesteps

echo -e "----staged rootfs with busybox,deps and devices------------------- \n"
tree ${OUTDIR}/rootfs
echo -e "------------------------------------------------------------------ \n"

# PSK-DONE: Clean and build the writer utility
buildfinderappsteps()
{
  buildsubstep "Building the finder app..." "skip"
      set -x
  cd ${FINDER_APP_DIR}
  make clean
  make CROSS_COMPILE=${CROSS_COMPILE}
      set +x
}
buildstep buildfinderappsteps


copyfinderappstep()
{
  buildsubstep "Copying finder app.." "skip"
      set -x
      cd /home/psk/coursera/aeld/assigns/assignments-3-and-later-psk73/
      cp ./finder-app/finder-test.sh $OUTDIR/rootfs/home
      cp ./finder-app/finder.sh $OUTDIR/rootfs/home
      cp ./finder-app/writer $OUTDIR/rootfs/home
      mkdir -p $OUTDIR/rootfs/home/conf
      cp ./finder-app/conf/username.txt $OUTDIR/rootfs/home/conf
      cp ./finder-app/conf/assignment.txt $OUTDIR/rootfs/home/conf
      cp ./finder-app/autorun-qemu.sh $OUTDIR/rootfs/home
      set +x
}
buildstep copyfinderappstep

createinitramfsstep()
{
  # PSK-DONE: Chown the root directory
  buildsubstep "Changing rootfs ownership to root" "skip"
      for f in `find ${OUTDIR}/rootfs`
      do
        set -x
        sudo chown -h root:root $f
        set +x
      done

  # PSK-DONE: Create initramfs.cpio.gz
  buildsubstep "Creating initramfs.cpio.gz" "skip"
      set -x
      cd "$OUTDIR/rootfs"
      find . |cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
      cd "$OUTDIR"
      sudo chown root:root initramfs.cpio
      sudo gzip -f initramfs.cpio
      set +x
}
buildstep createinitramfsstep


