#include <stdio.h>
#include <stdlib.h>
#include "util.h"
#include <sys/utsname.h>

static void oom(void)
{ 
	fprintf(stderr, "out of virtual memory\n");
	exit(2); 
}

void *xmalloc(size_t sz)
{
	void *p = calloc(sz,1); 
	if (!p) 
		oom();
	return p;
}

void *xrealloc(void *oldp, size_t sz)
{
	void *p = realloc(oldp,sz);
	if (!p) 
		oom();
	return p;
}

int kernel_version(void)
{
	struct utsname uts;
	int major, minor, patch; 

	if (uname(&uts) < 0) 
		return -1; 
	if (sscanf(uts.release, "%d.%d.%d", &major, &minor, &patch) != 3)
		return -1;
	return KRELEASE(major,minor,patch);
}
