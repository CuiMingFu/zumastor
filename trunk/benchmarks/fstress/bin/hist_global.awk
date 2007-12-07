BEGIN {
	numtabs = 1;
	values[0] = 0;
}
/histograms/ {
	for (value in values) {
		values[value] = 0;
	}
}
/HIST NFSPROC_/ {
	for (i=3 ; $i != "" ; i++) {
		split($i, pair, ":");
		value = pair[1];
		weight = pair[2];
		values[value] += weight;
	}
}
/SUMMARY/ {
	weighttotal = 0;
	for (value in values) {
		weighttotal += values[value];
	}
	weightsum = 0;
	pct = 0;
	last_pct_int = 0;
	for (value=0 ; value<1000 ; value += 0.1) {
		weightsum += values[value];
		pct = weightsum * 100 / weighttotal;
		if (int(pct) > last_pct_int + 1) {
			printf("%0.1f", value);
			for (j=0 ; j<numtabs ; j++) {
				printf("\t");
			}
			printf("%0.1f\n", pct);
			last_pct_int = int(pct);
		}
	}
	printf("\n");
	numtabs++;
}

