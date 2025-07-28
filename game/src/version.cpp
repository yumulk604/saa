#include <stdio.h>

void WriteVersion()
{
	FILE* fp = fopen("VERSION.txt", "w");

	if (fp)
	{
		fprintf(fp, "game: 40250\n");
		fclose(fp);
	}
}
