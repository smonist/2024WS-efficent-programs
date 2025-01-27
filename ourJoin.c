#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_CAPACITY  16000000
#define MAX_LINE_LEN  128
#define MAX_FIELDS    8
#define DELIM         ','


typedef struct {
    char *line;
    char *fields[MAX_FIELDS];
    int nfields;
} record_t;


static inline void read_csv_file(const char *filename,
                          record_t **records_out, size_t *count_out) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror(filename);
        exit(EXIT_FAILURE);
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("fstat");
        close(fd);
        exit(EXIT_FAILURE);
    }

    size_t filesize = sb.st_size;
    char *mapped = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap");
        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd); // Close the file descriptor after mapping

    const size_t capacity = MAX_CAPACITY;
    record_t *records = malloc(capacity * sizeof(record_t));
    if (!records) {
        fprintf(stderr, "Out of memory!\n");
        munmap(mapped, filesize);
        exit(EXIT_FAILURE);
    }
    size_t count = 0;

    char *line_start = mapped;
    char *line_end = mapped;

    while (line_end < mapped + filesize) {
        // Find the end of the line
        while (line_end < mapped + filesize && *line_end != '\n') {
            line_end++;
        }

        // Determine line length and handle edge cases
        size_t line_length = line_end - line_start;
        if (line_length > 0) {
            // Remove trailing carriage return if any
            if (line_end > line_start && *(line_end - 1) == '\r') {
                line_length--;
            }

            // Allocate memory for the line and copy its contents
            records[count].line = malloc(line_length + 1); // +1 for null terminator
            if (!records[count].line) {
                fprintf(stderr, "Out of memory!\n");
                munmap(mapped, filesize);
                free(records); // Free allocated records before exiting
                exit(EXIT_FAILURE);
            }
            memcpy(records[count].line, line_start, line_length);
            records[count].line[line_length] = '\0'; // Null-terminate the string

            // Split into fields
            records[count].nfields = 0;
            char *save = NULL;
            char *token = strtok_r(records[count].line, ",", &save);
            while (token && records[count].nfields < MAX_FIELDS) {
                records[count].fields[records[count].nfields++] = token;
                token = strtok_r(NULL, ",", &save);
            }

            count++;
        }

        // Move to the next line
        line_end++;
        line_start = line_end;
    }

    munmap(mapped, filesize); // Unmap the file
    *records_out = records;
    *count_out = count;
}

static inline void free_records(record_t *records, size_t count) {
    if (!records) return;
    for (size_t i = 0; i < count; i++) {
        free(records[i].line);
    }
    free(records);
}

static int g_sort_col;

int local_compare(const record_t *a, const record_t *b) {
    const char *fa = g_sort_col <= a->nfields ? a->fields[g_sort_col - 1] : "";
    const char *fb = g_sort_col <= b->nfields ? b->fields[g_sort_col - 1] : "";
    return strcmp(fa, fb);
}

void quicksort(record_t *records, int low, int high, int (*compare)(const record_t *, const record_t *)) {
    if (low < high) {
        record_t pivot = records[high];
        int i = low - 1;
        for (int j = low; j < high; j++) {
            if (compare(&records[j], &pivot) <= 0) {
                i++;
                record_t temp = records[i];
                records[i] = records[j];
                records[j] = temp;
            }
        }
        record_t temp = records[i + 1];
        records[i + 1] = records[high];
        records[high] = temp;

        int pivot_index = i + 1;

        quicksort(records, low, pivot_index - 1, compare);
        quicksort(records, pivot_index + 1, high, compare);
    }
}

static inline void sort_by_column(record_t *records, const size_t count, const int col) {
    g_sort_col = col;
    quicksort(records, 0, count - 1, local_compare);
}

static inline record_t *join_on_columns(const record_t *left, const size_t left_count, const int left_col,
                                 const record_t *right, const size_t right_count, const int right_col,
                                 size_t *out_count) {
    size_t cnt = 0;
    record_t *result = malloc(MAX_CAPACITY * sizeof(record_t));
    if (!result) {
        fprintf(stderr, "Out of memory in join!\n");
        exit(EXIT_FAILURE);
    }

    size_t i = 0, j = 0;
    while (i < left_count && j < right_count) {
        const char *lkey = left_col <= left[i].nfields ? left[i].fields[left_col - 1] : "";
        const char *rkey = right_col <= right[j].nfields ? right[j].fields[right_col - 1] : "";

        const int cmp = strcmp(lkey, rkey);
        if (cmp == 0) {
            size_t li = i;

            while (li < left_count &&
                   strcmp(lkey, left_col <= left[li].nfields ? left[li].fields[left_col - 1] : "") == 0) {
                size_t rj = j;
                while (rj < right_count &&
                       strcmp(rkey, right_col <= right[rj].nfields ? right[rj].fields[right_col - 1] : "") == 0) {

                    // Calculate the required buffer size for the joined line
                    size_t line_size = strlen(lkey) + 1; // Include delimiter

                    for (int lf = 0; lf < left[li].nfields; lf++) {
                        if (lf + 1 == left_col) continue;
                        line_size += strlen(left[li].fields[lf]) + 1;
                    }

                    for (int rf = 0; rf < right[rj].nfields; rf++) {
                        if ((rf + 1) == right_col) continue;
                        line_size += strlen(right[rj].fields[rf]) + 1;
                    }

                    // Allocate buffer for the joined line
                    char *line = malloc(line_size);
                    if (!line) {
                        fprintf(stderr, "Out of memory in join!\n");
                        exit(EXIT_FAILURE);
                    }

                    // Construct the joined line
                    char *ptr = line;
                    ptr += sprintf(ptr, "%s", lkey);

                    for (int lf = 0; lf < left[li].nfields; lf++) {
                        if (lf + 1 == left_col) continue;
                        ptr += sprintf(ptr, ",%s", left[li].fields[lf]);
                    }

                    for (int rf = 0; rf < right[rj].nfields; rf++) {
                        if ((rf + 1) == right_col) continue;
                        ptr += sprintf(ptr, ",%s", right[rj].fields[rf]);
                    }

                    // Populate the result record
                    result[cnt].line = line;
                    result[cnt].nfields = 0;

                    char *save = NULL;
                    char *token = strtok_r(line, ",", &save);
                    while (token && result[cnt].nfields < MAX_FIELDS) {
                        result[cnt].fields[result[cnt].nfields++] = token;
                        token = strtok_r(NULL, ",", &save);
                    }
                    cnt++;

                    if (cnt >= MAX_CAPACITY) {
                        fprintf(stderr, "Exceeded maximum capacity in join!\n");
                        exit(EXIT_FAILURE);
                    }

                    rj++;
                }
                li++;
            }

            while (i < left_count &&
                   strcmp(lkey, left_col <= left[i].nfields ? left[i].fields[left_col - 1] : "") == 0) {
                i++;
            }
            while (j < right_count &&
                   strcmp(rkey, right_col <= right[j].nfields ? right[j].fields[right_col - 1] : "") == 0) {
                j++;
            }
        } else if (cmp < 0) {
            i++;
        } else {
            j++;
        }
    }

    *out_count = cnt;
    return result;
}

static inline void print_records_as_csv(const record_t *records, const size_t count) {
    for (size_t i = 0; i < count; i++) {
        for (int f = 0; f < records[i].nfields; f++) {
            if (f > 0) printf(",");
            printf("%s", records[i].fields[f]);
        }
        printf("\n");
    }
}

int main(const int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s file1 file2 file3 file4\n", argv[0]);
        return EXIT_FAILURE;
    }

    record_t *f1_records = NULL;
    size_t f1_count = 0;
    read_csv_file(argv[1], &f1_records, &f1_count);
    sort_by_column(f1_records, f1_count, 1);

    record_t *f2_records = NULL;
    size_t f2_count = 0;
    read_csv_file(argv[2], &f2_records, &f2_count);
    sort_by_column(f2_records, f2_count, 1);

    size_t joined12_count = 0;
    record_t *joined12 = join_on_columns(f1_records, f1_count, 1,
                                         f2_records, f2_count, 1,
                                         &joined12_count);

    free_records(f1_records, f1_count);
    free_records(f2_records, f2_count);

    record_t *f3_records = NULL;
    size_t f3_count = 0;
    read_csv_file(argv[3], &f3_records, &f3_count);
    sort_by_column(f3_records, f3_count, 1);

    size_t joined123_count = 0;
    record_t *joined123 = join_on_columns(joined12, joined12_count, 1,
                                          f3_records, f3_count, 1,
                                          &joined123_count);

    free_records(joined12, joined12_count);
    free_records(f3_records, f3_count);

    sort_by_column(joined123, joined123_count, 4);

    record_t *f4_records = NULL;
    size_t f4_count = 0;
    read_csv_file(argv[4], &f4_records, &f4_count);
    sort_by_column(f4_records, f4_count, 1);

    size_t final_count = 0;
    record_t *final_join = join_on_columns(joined123, joined123_count, 4,
                                           f4_records, f4_count, 1,
                                           &final_count);

    free_records(joined123, joined123_count);
    free_records(f4_records, f4_count);

    print_records_as_csv(final_join, final_count);
    free_records(final_join, final_count);
    return 0;
}
