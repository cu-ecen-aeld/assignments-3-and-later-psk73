# Analysis of faulty-opps

## Introduction
This markdown file discusses a kernel oops caused by executing `echo "hello_world" > /dev/faulty` in a Linux system.

## Analysis
Dereferencing NULL Pointer: The `faulty_write` function in the `faulty` module is designed to produce a carsh by dereferencing a NULL pointer at:
 
```
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	__*(int *)0 = 0;__
	return 0;
}
```

## Error Details
Executing the command echo "hello_world" > /dev/faulty generates the following:

```
[   22.657187] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
[   22.657938] Mem abort info:
[   22.658131]   ESR = 0x0000000096000045
[   22.658384]   EC = 0x25: DABT (current EL), IL = 32 bits
[   22.658687]   SET = 0, FnV = 0
[   22.658878]   EA = 0, S1PTW = 0
[   22.659076]   FSC = 0x05: level 1 translation fault
[   22.660212] Data abort info:
[   22.660440]   ISV = 0, ISS = 0x00000045
[   22.660695]   CM = 0, WnR = 1
[   22.661245] user pgtable: 4k pages, 39-bit VAs, pgdp=0000000043db8000
[   22.661892] [0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
[   22.663563] Internal error: Oops: 96000045 [#1] PREEMPT SMP
[   22.664295] Modules linked in: hello(O) faulty(O) scull(O)
[   22.665831] CPU: 0 PID: 348 Comm: sh Tainted: G           O      5.15.124-yocto-standard #1
[   22.666264] Hardware name: linux,dummy-virt (DT)
[   22.666735] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[   22.667117] pc : faulty_write+0x18/0x20 [faulty]
[   22.668190] lr : vfs_write+0xf8/0x29c
[   22.668402] sp : ffffffc00c2fbd80
[   22.668572] x29: ffffffc00c2fbd80 x28: ffffff8002063700 x27: 0000000000000000
[   22.669013] x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
[   22.669358] x23: 0000000000000000 x22: ffffffc00c2fbdc0 x21: 000000558f576ba0
[   22.669707] x20: ffffff8003778f00 x19: 0000000000000012 x18: 0000000000000000
[   22.670034] x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
[   22.670386] x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
[   22.670734] x11: 0000000000000000 x10: 0000000000000000 x9 : ffffffc008268e3c
[   22.671134] x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
[   22.671929] x5 : 0000000000000001 x4 : ffffffc000b77000 x3 : ffffffc00c2fbdc0
[   22.672348] x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
[   22.672964] Call trace:
[   22.673160]  faulty_write+0x18/0x20 [faulty]
[   22.673473]  ksys_write+0x74/0x10c
[   22.673652]  __arm64_sys_write+0x24/0x30
[   22.673842]  invoke_syscall+0x5c/0x130
[   22.674033]  el0_svc_common.constprop.0+0x4c/0x100
[   22.674253]  do_el0_svc+0x4c/0xb4
[   22.674418]  el0_svc+0x28/0x80
[   22.674582]  el0t_64_sync_handler+0xa4/0x130
[   22.674791]  el0t_64_sync+0x1a0/0x1a4
[   22.675128] Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
[   22.676346] ---[ end trace 2a8446ee8025c83b ]---
Segmentation fault

```

# Conclusion
The kernel oops was triggered by dereferencing a NULL pointer in the faulty_write function. This show example of how not to write a device driver and how to highlights the severity of system faults and oops messages in Linux Kernel Development.
