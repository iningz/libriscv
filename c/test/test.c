#include <libriscv.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static struct timespec time_now();
static int64_t nanodiff(struct timespec start_time, struct timespec end_time);
static char *read_file(const char *filename, size_t *len);

static void error_callback(void *opaque, int type, const char *msg, long data)
{
	fprintf(stderr, "Error: %s (data: 0x%lX)\n", msg, data);
}

static void stdout_callback(void *opaque, const char *msg, unsigned len)
{
	printf("[libriscv] stdout: %.*s", (int)len, msg);
}

static void my_exit(RISCVMachine *m)
{
	RISCVRegisters *regs = libriscv_get_registers(m);
	#define REG_A0   10

	printf("Exit called! Status=%ld\n", regs->r[REG_A0]);
	libriscv_stop(m);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s [RISC-V elf file]\n", argv[0]);
		exit(1);
	}

    size_t size = 0;
	char *buffer = read_file(argv[1], &size);

	/* Create guest program arguments from argv[2...] */
	const unsigned g_argc = argc - 1;
	const char **g_argv = calloc(g_argc, sizeof(char *));
	g_argv[0] = "my_program";
	for (unsigned i = 1; i < g_argc; i++)
		g_argv[i] = argv[i + 1];

	/* RISC-V machine options */
	RISCVOptions options;
	libriscv_set_defaults(&options);
	options.max_memory = 4ULL << 30; // 4 GiB
	options.argc = g_argc;
	options.argv = g_argv;
	options.error = error_callback;
	options.stdout = stdout_callback;
	options.opaque = NULL;

	/* RISC-V machine */
	RISCVMachine *m = libriscv_new(buffer, size, &options);
	if (!m) {
		fprintf(stderr, "Failed to initialize the RISC-V machine!\n");
		exit(1);
	}

	/* A custom exit system call handler. WARNING: POSIX threads will not work right! */
	libriscv_set_syscall_handler(93, my_exit);

	struct timespec start_time = time_now();

	/* RISC-V execution, timing out after 5bn instructions */
	const int res = libriscv_run(m, 5000000000ull);
	if (res < 0) {
		fprintf(stderr, "Error during execution: %s\n", libriscv_strerror(res));
		exit(1);
	}

	struct timespec end_time = time_now();

	const int64_t retval = libriscv_return_value(m);
	const uint64_t icount = libriscv_instruction_counter(m);
	const int64_t nanos = nanodiff(start_time, end_time);

	libriscv_delete(m);

	printf(">>> Program exited, exit code = %" PRId64 " (0x%" PRIX64 ")\n",
		retval, (uint64_t)retval);
	printf("Instructions executed: %" PRIu64 "  Runtime: %.3fms  Insn/s: %.0fmi/s\n",
		icount, nanos/1e6,
		icount / (nanos * 1e-3));
}

struct timespec time_now()
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t;
}
int64_t nanodiff(struct timespec start_time, struct timespec end_time)
{
	return (end_time.tv_sec - start_time.tv_sec) * (int64_t)1e9 + (end_time.tv_nsec - start_time.tv_nsec);
}
char *read_file(const char *filename, size_t *size)
{
    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
		fprintf(stderr, "Could not open file: %s\n", filename);
		exit(1);
	}

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

	char *buffer = malloc(*size);
    if (*size != fread(buffer, 1, *size, f))
    {
        fclose(f);
		fprintf(stderr, "Could not read file: %s\n", filename);
		exit(1);
    }
    fclose(f);
	return buffer;
}
