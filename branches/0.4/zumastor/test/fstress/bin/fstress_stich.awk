BEGIN {
}
END {
	c = 0; 
	for (try in trycnt) {
		a[c++] = try;
	}
	for (i=0 ; i<c ; i++) {
		for (j=0 ; j<i ; j++) {
			if (a[j]*1.0 > a[i]*1.0) {
				t = a[j];
				a[j] = a[i];
				a[i] = t;
			}
		}
	}

	for (i=0 ; try=a[i] ; i++) {
		if (Wtrycnt[try] == 0) {
			Wtrycnt[try] = -1;
		}
		if (trycnt[try] == 0) {
			trycnt[try] = -1;
		}
	}

	printf("A\t\tWtry\tWgot\tWlat\t\ttry\tgot\tlat\n");
	for (i=0 ; try=a[i] ; i++) {
		printf("%d\t", try);
		printf(":\t");
		printf("%d\t", Wtrysum[try]);
		printf("%d\t", Wgotsum[try]);
		printf("%0.2f\t", Wlatsum[try]/Wtrycnt[try]);
		printf(":\t");
		printf("%d\t", trysum[try]);
		printf("%d\t", gotsum[try]);
		printf("%0.2f\t", latsum[try]/trycnt[try]);
		printf("%0.2f\t", latmin[try]);
		printf("%0.2f\n", latmax[try]);
	}

}
/WARMUP:/ {
	try = $3;
	got = $9;
	lat = $12;

	Wtrycnt[try] += 1;
	Wtrysum[try] += try;
	Wgotsum[try] += got;
	Wlatsum[try] += lat;
}
/SUMMARY/ {
	try = $3;
	got = $9;
	lat = $12;

	trycnt[try] += 1;
	trysum[try] += try;
	gotsum[try] += got;
	latsum[try] += lat;
	if (latmin[try] == 0 || latmin[try] > lat) {
		latmin[try] = lat;
	}
	if (latmax[try] == 0 || latmax[try] < lat) {
		latmax[try] = lat;
	}
}
