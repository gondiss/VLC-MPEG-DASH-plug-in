#!/bin/awk -f
BEGIN {
  x = 2*1024*1024;
  ct1 = 123774 * 8;
  ct2 = 0 * 8;
  t2 = 800000;
  lastChunkBps = 0;

  bpsAvg1 = 0;
  bpsLC1 = 0;
  bL1 = 0;
  pbBR1 = 0;

  bpsAvg2 = 0;
  bpsLC2 = 0;
  bL2 = 0;
  pbBR2 = 0;

  identity1 = "=======START===========0x8a0b880=================";
  identity2 = "=======START===========0x8a0b938=================";
  state = 1;

  print "Time", "BL1", "bpsAvg1", "bpsLC1", "pbBR1", "BL2", "bpsAvg2", "bpsLC2", "pbBR2","ABW";
}
{
  if ($1 == identity1) {
    state = 1;
  }
  if ($1 == identity2) {
    state = 2;
  }

  if ($1 == "Time:" && $3 == "BufferPercent:") {
    if (state == 1) {
      bL1 = $4;
      bpsAvg1 = $7/1000;
      bpsLC1 = $11/1000;
      pbBR1 = $14/1000;
    }
    else {
      bL2 = $4;
      bpsAvg2 = $7/1000;
      bpsLC2 = $11/1000;
      pbBR2 = $14/1000;
    }
    if ($2 < t2)
      print $2, bL1,bpsAvg1,bpsLC1,pbBR1,bL2,bpsAvg2,bpsLC2,pbBR2, (x-ct1)/1000;
    else
      print $2, bL1,bpsAvg1,bpsLC1,pbBR1,bL2,bpsAvg2,bpsLC2,pbBR2, (x-ct1-ct2)/1000;
    lastChunkBps = $11;
  }
}
END {
}


