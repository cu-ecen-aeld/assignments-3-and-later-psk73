#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
# Author: Sreekanth Pasumarthy

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
CROSS_COMPILE_ROOT=/home/psk/coursera/aeld/armcc/arm-gnu-toolchain-13.2.Rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu

if [ $# -lt 1 ]; then
  echo -e "Using default directory ${OUTDIR} for output \n"
else
  OUTDIR=$1
  echo -e "Using passed directory ${OUTDIR} for output \n"
fi

mkdir -p ${OUTDIR}

#check if script was called to run without interrupts
#default to noask, as that is requirement for assignment
STEPACTION="noask"
if [ $# -gt 1 ]; then
  if test $2 == "ask"; then
    STEPACTION="ask"
  fi
fi

ECHOONLY="no"
if [ $# -gt 2 ]; then
  if test $3 == "yes"; then
    ECHOONLY="yes"
  fi
fi

#Echo build step.
#execute or skip or exit
buildstep() {
  steplevel=$1
  echo -e "Step lvel ${steplevel}\n"
  if [ $steplevel -eq 1 ]; then
    echo -e "--Step:  $2 \n"
    indent=">>>>"
  else
    echo -e "----Sub Step: $2 \n"
    indent=">>>>>>"
  fi

  if test $STEPACTION = "ask"; then
    read -p "${indent} How do you want to proceed (y/s/n)?  " answer
    case ${answer:0:1} in
    y | Y)
      true
      ;;
    s | S)
      echo -e "${indent} Skipping\n"
      echo -e "${indent} ${*:3:$#}\n"
      return
      ;;
    *)
      echo -e "${indent} Exiting now\n"
      exit
      ;;
    esac
  fi

  #execute the step
  echo -e "${indent} Running\n"
  if [ $ECHOONLY == "yes" ]; then
    echo -e "${indent} ${*:3:$#}\n"
    #run step1 so substeps can echo
    if [ $steplevel -eq 1 ]; then
      ${*:3:$#}
    fi
  else
    #run the step or sub-step
    ${*:3:$#}
  fi
}

cd "$OUTDIR"

buildlinuxstep() {
  if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    #clone linux
    clonelinuxstep() {
      cd "$OUTDIR"
      if [ ! -d "${OUTDIR}/linux-stable" ]; then
        #Clone only if the repository does not exist.
        echo -e "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR} \n"
        set -x
        git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
        set +x
      fi
    }
    buildstep 2 "Check and clone linux" clonelinuxstep

    #checkout to version
    checkoutlinuxstep() {
      cd "$OUTDIR"
      cd linux-stable
      set -x
      git checkout ${KERNEL_VERSION}
      set +x
    }
    buildstep 2 "Checking out version ${KERNEL_VERSION}" checkoutlinuxstep

    # PSK-DONE: Add your kernel build steps here

    #deep clean
    mrproperstep() {
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
      set +x
    }
    buildstep 2 "Deep Cleaning the kernel workspace" mrproperstep

    #build defconfig for "virt" arm dev board
    defconfigstep() {
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
      set +x
    }
    buildstep 2 "Building def config for virt arm dev board" defconfigstep

    #build vmlinux target
    buildkernelstep() {
      set -x
      make -j2 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
      set +x
    }
    buildstep 2 "Build kernel image for virt arm dev" buildkernelstep

    #build modules
    buildmodeulesstep() {
      set -x
      #make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules
      set +x
    }
    buildstep 2 "Build modules" buildmodeulesstep

    #build device tree
    builddtbstep() {
      set -x
      make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs
      set +x
    }
    buildstep 2 "Build the devicetree" builddtbstep

    copylinuximagetooutdir()
    {
       cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}
    }
    buildstep 2 "Copy linux image to outdir" copylinuximagetooutdir
  else
    echo -e "Image already built. Nothing to do\n"
  fi
  
}
buildstep 1 "Build Linux Steps" buildlinuxstep

rootfsstagingstep() {
  cd "$OUTDIR"
  checkforprevrootfs()
  {
    if [ -d "${OUTDIR}/rootfs" ]; then
      echo -e "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
      set -x
      sudo rm -rf ${OUTDIR}/rootfs
      set +x
    fi
  }
  buildstep 2 "Check previous rootfs" checkforprevrootfs

  # PSK-DONE: Create necessary base directories
  makemptyrootfs()
  {
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
  buildstep 2 "Make empty rootfs" makemptyrootfs
}
buildstep 1 "Create empty rootfs staging step" rootfsstagingstep

busyboxstep() {
  cd "$OUTDIR"
  clonebusyboxstep()
  {
    if [ ! -d "${OUTDIR}/busybox" ]; then
      set -x
      git clone git://busybox.net/busybox.git
      cd busybox
      git checkout ${BUSYBOX_VERSION}
      set +x
      # TODO:  Configure busybox
    else
      echo -e "Busybox dir already exists"
    fi
  }
  buildstep 2 "Cloning busybox" clonebusyboxstep

  # PSK-DONE: Make and install busybox
  distcleanbusyboxstep() {
    cd $OUTDIR/busybox
    set -x
    make distclean
    set +x
  }
  buildstep 2 "Busybox build step1: clean" distcleanbusyboxstep

  buildbusyboxdefconfigstep() {
    cd $OUTDIR/busybox
    set -x
    set -x
    make defconfig
    set +x
  }
  buildstep 2 "Busybox build step2: defconfig" buildbusyboxdefconfigstep

  compilebusyboxstep() {
    cd $OUTDIR/busybox
    set -x
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
    set +x
  }
  buildstep 2 "Busybox build step3: cross compile" compilebusyboxstep

  installbusyboxinrootfsstep() {
    cd $OUTDIR/busybox
    set -x
    make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
    set +x
  }
  buildstep 2 "Busybox build step4: install" installbusyboxinrootfsstep

}
buildstep 1 "Build and install busybox" busyboxstep

libdependencysteps() {
  # PSK-DONE: Add library dependencies to rootfs
  getlibdependencystep()
  {
    cd ${OUTDIR}/rootfs
    echo -e "Library dependencies"
    set -x
    ${CROSS_COMPILE}readelf -a ./bin/busybox | grep "program interpreter"
    ${CROSS_COMPILE}readelf -a ./bin/busybox | grep "Shared library"
    set +x
  }
  buildstep 2 "Get libdependcies" getlibdependencystep

  copylibdependenciesstep()
  {
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
  buildstep 2 "Copy lib dependencies to rootfs" copylibdependenciesstep
}
buildstep 1 "Copying library dependencies to rootfs" libdependencysteps

# PSK-DONE: Make device nodes
makenodesteps() {
  makenodes()
  {
  set -x
  cd ${OUTDIR}/rootfs
  sudo mknod -m 666 ./dev/null c 1 3
  sudo mknod -m 666 ./dev/console c 5 1
  set +x
  }
  buildstep 2 "Making nodes in rootfs" makenodes
}
buildstep 1 "Make node steps" makenodesteps

echo -e "----staged rootfs with busybox,deps and devices------------------- \n"
tree ${OUTDIR}/rootfs
echo -e "------------------------------------------------------------------ \n"

# PSK-DONE: Clean and build the writer utility
finderappsteps() {
  buildapp()
  {
    set -x
    cd ${FINDER_APP_DIR}
    make clean
    make CROSS_COMPILE=${CROSS_COMPILE}
    set +x
  }
  buildstep 2 "Build finder app" buildapp

  copyfinderappstep() {
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
  buildstep 2 "Copy finder app step" copyfinderappstep
}
buildstep 1 "Build and copy finder app steps" finderappsteps

createinitramfsstep() {
  # PSK-DONE: Chown the root directory
  rootfschownstep() {
    for f in $(find ${OUTDIR}/rootfs); do
      set -x

      sudo chown -h root:root $f
      set +x
    done
  }
  buildstep 2 "Changing rootfs ownership to root" rootfschownstep

  # PSK-DONE: Create initramfs.cpio.gz
  createinitramfsstep() {
    set -x
    cd "$OUTDIR/rootfs"
    find . | cpio -H newc -ov --owner root:root >${OUTDIR}/initramfs.cpio
    cd "$OUTDIR"
    sudo chown root:root initramfs.cpio
    sudo gzip -f initramfs.cpio
    set +x
  }
  buildstep 2 "Creating initramfs.cpio.gz" createinitramfsstep

}
buildstep 1 "Create initramfs" createinitramfsstep