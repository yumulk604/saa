#include <stdio.h>

void WriteVersion()
{
	FILE* fp = fopen("VERSION.txt", "w");

	if (fp)
	{
		fprintf(fp, "db: 40250\n");
		fclose(fp);
	}
}

