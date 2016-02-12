// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

    if (!(err & FEC_WR)) {
        if (err & FEC_U) cprintf("has FEC_U"); else cprintf(" not FEC_U");
        if (err & FEC_PR) cprintf(" has FEC_PR"); else cprintf(" not FEC_PR");
        cprintf(" fault addr = %08x\n", addr);
        cprintf(" fault eip = %08x\n", utf->utf_eip);
        panic("pgfault: need FEC_WR");
    }
    
    pte_t pte = uvpt[PGNUM(addr)];
    if ((PTE_U|PTE_P) != (pte & (PTE_U|PTE_P))) {
        panic("pgfault: need PTE_U | PTE_P");
    }

    if (!(pte & PTE_COW)) {
        panic("pgfault: need PTE_COW");
    }

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
    if ((r = sys_page_alloc(0, PFTEMP, PTE_U | PTE_P | PTE_W)) < 0) {
        panic("pgfault: sys_page_alloc %e", r);
    }

    memmove(PFTEMP, (void *)ROUNDDOWN(addr, PGSIZE), PGSIZE);

    if ((r = sys_page_map(0, PFTEMP, 0, (void *)ROUNDDOWN(addr, PGSIZE), PTE_U | PTE_P | PTE_W)) < 0) {
        panic("pgfault: sys_page_map %e", r);
    }

    if ((r = sys_page_unmap(0, PFTEMP)) < 0) {
        panic("pgfault: sys_page_unmap");
    }

	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	// LAB 4: Your code here.
	int r;
    pte_t pte;
    void *addr = (void *)(pn * PGSIZE);

    pte = uvpt[pn];
    if ((PTE_U|PTE_P) != (pte & (PTE_U|PTE_P))) {
        return -E_INVAL;
    }

    // read-only or shared pages
    if (!(pte & (PTE_W | PTE_COW)) || (pte & PTE_SHARE)) {
        if ((r = sys_page_map(0, addr, envid, addr, pte & PTE_SYSCALL)) < 0) {
            panic("duppage: read-only pages %e", r); 
        }    

        return r;
    }
    
    r = sys_page_map(0, addr, envid, addr, ((pte & PTE_SYSCALL) | PTE_COW) & (~PTE_W));
    if (r < 0) {
        cprintf("pgoff %d\n", ((pte & PTE_SYSCALL) | PTE_COW) & (~PTE_W));
        panic("duppage: sys_page_map, env_id = %d, %d %e", envid, r, r);
    }

    r = sys_page_map(0, addr, 0, addr, (pte | PTE_COW) & (~PTE_W));
    if (r < 0) {
        panic("duppage: sys_page_map(0, addr, 0, addr, PGOFF(pte));");
    }

	// panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
    set_pgfault_handler(pgfault);
    
    int env_id = sys_exofork();
    
    if (env_id < 0) {
        panic("fork error: %e", env_id);
    }

    if (env_id == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
    }

    int i, j;
    int pn;
    int r;

    for (i=0; i<PDX(UTOP); i++) {
        if (!(uvpd[i] & PTE_P))
            continue;

        for (j=0; j<NPTENTRIES; j++) {
            pn = i * NPTENTRIES + j;
            if (!(uvpt[pn] & PTE_P))
                continue;

            if (pn == PGNUM(UXSTACKTOP - PGSIZE))
                continue;

            if ((r = duppage(env_id, pn)) < 0) {
                panic("fork: duppage");
            }
        }
    }

    if ((r = sys_env_set_pgfault_upcall(env_id, thisenv->env_pgfault_upcall)) < 0) {
        panic("sys_env_set_pgfault_upcall error: %e", r);
    }

    if ((r = sys_page_alloc(env_id, (void *)ROUNDDOWN(UXSTACKTOP - 1, PGSIZE), PTE_P|PTE_U|PTE_W)) < 0) {
        panic("fork error: %e", r);
    }

	// Start the child environment running
	if ((r = sys_env_set_status(env_id, ENV_RUNNABLE)) < 0)
		panic("fork error: %e", r);

    return env_id;
	// panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
