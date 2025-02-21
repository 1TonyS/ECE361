#define MAX_FILEDATA_SIZE 1000 // Maximum file data bytes per packet

struct packet {
    unsigned int total_frag;
    unsigned int frag_no;
    unsigned int size;
    char *filename;  
    char filedata[MAX_FILEDATA_SIZE];
};