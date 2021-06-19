#!/bin/bash

origs=(FileA.bin FileB.bin FileC.bin)
receiveds=(RecevedFileA.bin RecevedFileB.bin RecevedFileC.bin)

# Clean up.
rm "${receiveds[@]}"

# Do the test.
./mqrecv &
./mqsend "${origs[@]}"

# Verify the result.
for i in 0 1 2; do
	orig=$(md5sum ${origs[$i]})
	received=$(md5sum ${receiveds[$i]})

	set -- $orig
	md_orig=$1

	set -- $received
	md_received=$1

	if [ ${md_orig} = ${md_received} ]; then
		echo Success!
	fi
done