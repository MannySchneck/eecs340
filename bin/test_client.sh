#! /bin/bash

# run clients and save output:

OUTPUT_DIR="test-output"

PORT=80
SERVER_PATH="/index.html"
STACK="k"
RC=0
WIN=0


declare -a urls=("www.google.com" "www.northwestern.edu" "www.eecs.northwestern.edu" "www.cs.northwestern.edu" "www.northwestern.edu")

echo "Running on paths: "
for((i=0; i<${#urls[@]}; i++))
do
    urls[$i]="$STACK ${urls[$i]} $PORT $SERVER_PATH"
    echo "${urls[$i]}"
done

echo

for((i=0; i<${#urls[@]}; i++))
do
    ./http_client ${urls[$i]}      &> $OUTPUT_DIR/output.mine-$i
    ./http_client.marc ${urls[$i]} &> $OUTPUT_DIR/output.marc-$i

    diff $OUTPUT_DIR/output.marc-$i $OUTPUT_DIR/output.mine-$i &>/dev/null
    RC=$?

    if [[ $RC == 0 ]]
    then
        echo "SUCCESS:"
        echo "${urls[$i]} gave same output."
    else
        echo "FAILURE:"
        echo "${urls[$i]} gave different output. Here's the diff:"

        cmp  $OUTPUT_DIR/output.marc-$i $OUTPUT_DIR/output.mine-$i
        WIN=-1
    fi
    echo
    echo
done

if [[ $WIN  == 0 ]] 
then
    echo "Passed all tests =)"
else
    echo "There was sadness"
fi
