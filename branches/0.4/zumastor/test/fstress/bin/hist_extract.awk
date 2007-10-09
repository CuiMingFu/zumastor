BEGIN {
	numtabs = 1;
}
/HIST NFSPROC_READ / {
	weighttotal = 0;
	for (i=3 ; $i != "" ; i++) {
		split($i, pair, ":");
		value = pair[1];
		weight = pair[2];
		weighttotal += weight;
	}
	weightsum = 0;
	pct = 0;
	last_pct_int = 0;
	total = 0;
	count = 0;
	for (i=3 ; $i != "" ; i++) {
		split($i, pair, ":");
		value = pair[1];
		weight = pair[2];
		weightsum += weight;
		pct = weightsum*100/weighttotal;
		if (int(pct) > last_pct_int + 1) {
			printf("%0.1f", value);
			for (j=0 ; j<numtabs ; j++) {
				printf("\t");
			}
			printf("%0.1f\n", pct);
			last_pct_int = int(pct);
		}
		total += value * weight;
		count += weight;
	}
	printf("average=%0.2f ", total / count);
}
/SUMMARY/ {
	printf("this_client=%d total=%d\n", $3, $3 * $15);
	numtabs++;
}

