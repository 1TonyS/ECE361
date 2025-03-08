#define MAX_FILEDATA_SIZE 1000 // Maximum size for file data in each packet
#define FILENAME_SIZE 100      // Maximum filename size

// Updated structure: filename is now a pointer.
struct packet {
    unsigned int total_frag;
    unsigned int frag_no;
    unsigned int size;
    char *filename;   // Dynamically allocated filename string.
    char filedata[MAX_FILEDATA_SIZE];
};
