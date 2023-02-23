#include <stdio.h>

int main()
{
    FILE *fp;

    fp = fopen("file.txt", "w+");
    fputs("This is runoob.com", fp);

    fseek(fp, 7, SEEK_SET);
    fputs(" Csdfsfdsfsdfsdfsdfsdfsdf", fp);
    fclose(fp);

    return (0);
}