#
# input: {value, weight} pairs.
#
BEGIN {
	count = 0;
}
{
	values[count] = $1;
	weights[count] = $2;
	count += 1;
}
END {
	#
	# gather similar values together into buckets.
	#
	if (gather_values) {
		factor = 1 + (gather_values / 100);
		for (i=0 ; i<count ; i++) {
			for (j=i+1 ; j<count ; j++) {
				if ((values[i] > values[j] && \
				     values[i] < values[j] * factor) || \
				    (values[i] < values[j] && \
				     values[i] * factor > values[j])) {
					weights[i] += weights[j];
					values[j] = 0;
					weights[j] = 0;
				}
			}
		}
	}

	#
	# make smallest value "1" and all others relative to it.
	#
	if (normalize_values) {
		minval = 0;
		for (i=0 ; i<count ; i++) {
			if (minval == 0 || values[i] < minval) {
				minval = values[i];
			}
		}
		if (minval > 0) {
			for (i=0 ; i<count ; i++) {
				values[i] = values[i] / minval;
			}
		}
	}

	#
	# convert values to integers.
	#
	if (trunc_values) {
		for (i=0 ; i<count ; i++) {
			values[i] = int(values[i]);
		}
	}

	#
	# convert weights to integers.
	#
	if (trunc_weights) {
		for (i=0 ; i<count ; i++) {
			weights[i] = int(weights[i]);
		}
	}

	#
	# combine like values, summing their weights.
	#
	if (aggregate_values) {
		for (i=0 ; i<count ; i++) {
			for (j=0 ; j<i ; j++) {
				if (values[j] == values[i]) {
					weights[j] += weights[i];
					values[i] = 0;
					weights[i] = 0;
				}
			}
		}
	}

	#
	# convert weights to percents (sum to 100).
	#
	if (percent_weights) {
		weight_sum = 0;
		for (i=0 ; i<count ; i++) {
			weight_sum += weights[i];
		}
		if (weight_sum) {
			for (i=0 ; i<count ; i++) {
				weights[i] = weights[i] * 100 / weight_sum;
			}
		}
	}

	#
	# output only a weighted average.
	#
	if (weighted_average) {
		weight_sum = 0;
	        for (i=0 ; i<count ; i++) {
			total += values[i] * weights[i];
			weight_sum += weights[i];
		}
		if (weight_sum) {
			printf("%0.2f\n", total / weight_sum);
		} else {
			printf("%0.2f\n", 0);
		}
		exit;
	}

	#
	# output {value, weight} pairs.
	#
	for (i=0 ; i<count ; i++) {
		if (values[i] == 0 && weights[i] == 0) continue;

		if (nfsop_values) {
			if (values[i] == 0) printf("null");
			if (values[i] == 1) printf("getattr");
			if (values[i] == 2) printf("setattr");
			if (values[i] == 3) printf("lookup");
			if (values[i] == 4) printf("access");
			if (values[i] == 5) printf("readlink");
			if (values[i] == 6) printf("read");
			if (values[i] == 7) printf("write");
			if (values[i] == 8) printf("create");
			if (values[i] == 9) printf("mkdir");
			if (values[i] == 10) printf("symlink");
			if (values[i] == 11) printf("mknod");
			if (values[i] == 12) printf("remove");
			if (values[i] == 13) printf("rmdir");
			if (values[i] == 14) printf("rename");
			if (values[i] == 15) printf("link");
			if (values[i] == 16) printf("readdir");
			if (values[i] == 17) printf("readdirplus");
			if (values[i] == 18) printf("fsstat");
			if (values[i] == 19) printf("fsinfo");
			if (values[i] == 20) printf("pathconf");
			if (values[i] == 21) printf("commit");
			if (values[i] == 106) printf("seqread");
			if (values[i] == 107) printf("seqwrite");
			if (values[i] == 108) printf("appendwrite");
		} else if (trunc_values) {
			printf("%d", values[i]);
		} else {
			printf("%0.2f", values[i]);
		}

		printf("\t");

		if (trunc_weights) {
			printf("%d", weights[i]);
		} else {
			printf("%0.2f", weights[i]);
		}

		printf("\n");
	}

}
