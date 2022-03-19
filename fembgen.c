#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(int argc, char** argv) {
	if(argc != 4) return !!printf("need 1 file to generate embed for, size name, and data name\n");
	int fd = open(argv[1], O_RDONLY, 0644);
	if(fd < 0) return 1;
	struct stat s;
	fstat(fd, &s);
	printf(
		"#include <stdint.h>\n"
		"#define %s ((unsigned long)%llu)\n"
		"uint8_t %s[%s]={",
		argv[3],
		s.st_size,
		argv[2],
		argv[3]
	);
	uint8_t mem[s.st_size];
	read(fd, mem, s.st_size);
	for(unsigned long l = 0; l < s.st_size; l++) printf("%hu,", (uint16_t)mem[l]);
	puts("};");
}
