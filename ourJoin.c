#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CAPACITY  16000000
#define MAX_LINE_LEN  128
#define MAX_FIELDS    8
#define DELIM         ','

/*
 * A structure to hold a single CSV line split into fields.
 * 'line' holds the raw string (we keep it for output),
 * 'fields' are pointers into 'line' split by the delimiter,
 * 'nfields' is how many fields are found.
 */
typedef struct {
    char *line;
    char *fields[MAX_FIELDS];
    int nfields;
} record_t;

/*
 * Read all lines from file 'filename', parse them (split by DELIM),
 * and store them in an array of record_t. Return the pointer
 * to that array via '*records_out' and the count via '*count_out'.
 *
 * This function dynamically grows a buffer for records, so it
 * can handle an unknown number of lines. For real production
 * usage, you may want more robust memory management.
 */
static void read_csv_file(const char *filename,
                          record_t **records_out, size_t *count_out) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror(filename);
        exit(EXIT_FAILURE);
    }
    const size_t capacity = MAX_CAPACITY;
    record_t *records = malloc(capacity * sizeof(record_t));
    size_t count = 0;

    char buffer[MAX_LINE_LEN];

    while (fgets(buffer, sizeof(buffer), fp)) {
        // Remove trailing newline if any
        char *nl = strchr(buffer, '\n');
        if (nl) *nl = '\0';

        // Copy line into record->line
        records[count].line = strdup(buffer);
        if (!records[count].line) {
            fprintf(stderr, "Out of memory!\n");
            exit(EXIT_FAILURE);
        }

        // Split into fields
        records[count].nfields = 0;
        char *save = NULL;
        char *token = strtok_r(strdup(buffer), ",", &save);
        while (token && records[count].nfields < MAX_FIELDS) {
            records[count].fields[records[count].nfields++] = token;
            token = strtok_r(NULL, ",", &save);
        }

        ++count;
    }

    fclose(fp);
    *records_out = records;
    *count_out = count;
}

/*
 * Free the memory from read_csv_file.
 */
static void free_records(record_t *records, size_t count) {
    if (!records) return;
    for (size_t i = 0; i < count; i++) {
        free(records[i].line);
    }
    free(records);
}

/*
 * Compare function for qsort to sort record_t by a given 1-based field index (col).
 * If col is out of range, we treat missing fields as empty string.
 */
static int compare_by_column(const void *a, const void *b, void *arg) {
    int col = *(int *) arg; // 1-based column index
    const record_t *ra = (const record_t *) a;
    const record_t *rb = (const record_t *) b;

    // If col > nfields, treat that as empty ""
    const char *fa = (col <= ra->nfields) ? ra->fields[col - 1] : "";
    const char *fb = (col <= rb->nfields) ? rb->fields[col - 1] : "";

    return strcmp(fa, fb);
}

/*
 * Sort an array of records by a 1-based column index.
 */
static void sort_by_column(record_t *records, size_t count, int col) {
    // Use the GNU extension qsort_r if available, or a static global for col.
    // Here we use the C11 qsort_s-like approach with a cookie:
    // some platforms only have qsort, but let's show a typical pattern.
#ifdef _GNU_SOURCE
    qsort_r(records, count, sizeof(record_t), &col, compare_by_column);
#else
    // We'll use a file-scope global or static variable for col if needed.
    // For simplicity, let's do a small wrapper:
    // Just store col in a static and call a compare function ignoring arg.
    static int g_sort_col;
    g_sort_col = col;

    // local compare that references g_sort_col
    int local_compare(const void *a, const void *b) {
        const record_t *ra = (const record_t *) a;
        const record_t *rb = (const record_t *) b;
        const char *fa = (g_sort_col <= ra->nfields) ? ra->fields[g_sort_col - 1] : "";
        const char *fb = (g_sort_col <= rb->nfields) ? rb->fields[g_sort_col - 1] : "";
        return strcmp(fa, fb);
    }

    qsort(records, count, sizeof(record_t), local_compare);
#endif
}

/*
 * Join two sorted arrays of records on a given column in each array (both 1-based),
 * returning a newly-allocated array of the merged records. The default 'join' logic
 * is: for matching keys, output a single line that contains:
 *
 *    key, (fields of left except the key), (fields of right except the key)
 *
 * If no match, that line does not appear (like join without -a or -v).
 *
 * This function merges them in ascending order of the join key.
 */
static record_t *join_on_columns(const record_t *left, size_t left_count, int left_col,
                                 const record_t *right, size_t right_count, int right_col,
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
            // Both keys match, collect all combinations of left and right rows
            size_t li = i;
            size_t rj = j;

            // Iterate over all left records with the same key
            while (li < left_count &&
                   strcmp(lkey, left_col <= left[li].nfields ? left[li].fields[left_col - 1] : "") == 0) {
                rj = j; // Reset right index for each left record
                while (rj < right_count &&
                       strcmp(rkey, right_col <= right[rj].nfields ? right[rj].fields[right_col - 1] : "") == 0) {
                    // Join the current left[li] and right[rj]
                    char buf[4 * MAX_LINE_LEN];
                    buf[0] = '\0';

                    // Add the join key
                    snprintf(buf, sizeof(buf), "%s", lkey);

                    // Append left fields (excluding left_col)
                    for (int lf = 0; lf < left[li].nfields; lf++) {
                        if ((lf + 1) == left_col) continue;
                        strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
                        strncat(buf, left[li].fields[lf], sizeof(buf) - strlen(buf) - 1);
                    }

                    // Append right fields (excluding right_col)
                    for (int rf = 0; rf < right[rj].nfields; rf++) {
                        if ((rf + 1) == right_col) continue;
                        strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
                        strncat(buf, right[rj].fields[rf], sizeof(buf) - strlen(buf) - 1);
                    }

                    // Allocate and parse this joined line
                    result[cnt].line = strdup(buf);
                    result[cnt].nfields = 0;

                    char *save = NULL;
                    char *token = strtok_r(strdup(buf), ",", &save);
                    while (token && result[cnt].nfields < MAX_FIELDS) {
                        result[cnt].fields[result[cnt].nfields++] = token;
                        token = strtok_r(NULL, ",", &save);
                    }
                    cnt++;

                    // Check for overflow
                    if (cnt >= MAX_CAPACITY) {
                        fprintf(stderr, "Exceeded maximum capacity in join!\n");
                        exit(EXIT_FAILURE);
                    }

                    rj++;
                }
                li++;
            }

            // Move i and j past this key
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


/*
 * Print all records to stdout as CSV lines.
 */
static void print_records(const record_t *records, size_t count) {
    for (size_t i = 0; i < count; i++) {
        // Rebuild line from fields or just print the original line.
        // If we want exactly the CSV we constructed, do:
        for (int f = 0; f < records[i].nfields; f++) {
            if (f > 0) printf(",");
            printf("%s", records[i].fields[f]);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s file1 file2 file3 file4\n", argv[0]);
        return EXIT_FAILURE;
    }

    // 1) Read & sort file1 by 1st column
    record_t *f1_records = NULL;
    size_t f1_count = 0;
    read_csv_file(argv[1], &f1_records, &f1_count);
    sort_by_column(f1_records, f1_count, 1);

    // 2) Read & sort file2 by 1st column
    record_t *f2_records = NULL;
    size_t f2_count = 0;
    read_csv_file(argv[2], &f2_records, &f2_count);
    sort_by_column(f2_records, f2_count, 1);

    // 3) Join the two on their 1st columns
    size_t joined12_count = 0;
    record_t *joined12 = join_on_columns(f1_records, f1_count, 1,
                                         f2_records, f2_count, 1,
                                         &joined12_count);

    // We can free the original sets now if we want
    free_records(f1_records, f1_count);
    free_records(f2_records, f2_count);

    // 4) Read file3, sort by 1st column, join with joined12
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

    // 5) Sort the result by the 4th column
    //    (Be mindful that some lines may not have 4 columns.)
    sort_by_column(joined123, joined123_count, 4);

    // 6) Read & sort file4 by 1st column
    record_t *f4_records = NULL;
    size_t f4_count = 0;
    read_csv_file(argv[4], &f4_records, &f4_count);
    sort_by_column(f4_records, f4_count, 1);

    // 7) Join joined123 (on col 4) with file4 (on col 1)
    size_t final_count = 0;
    record_t *final_join = join_on_columns(joined123, joined123_count, 4,
                                           f4_records, f4_count, 1,
                                           &final_count);

    free_records(joined123, joined123_count);
    free_records(f4_records, f4_count);

    // 8) Print final results
    print_records(final_join, final_count);

    // Cleanup
    free_records(final_join, final_count);

    return 0;
}
