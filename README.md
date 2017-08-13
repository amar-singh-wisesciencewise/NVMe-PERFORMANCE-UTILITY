# NVMe-PERFORMANCE-UTILITY
This is an Utility measures the performance of NVMe Drives.
This Utility measures the following parameters:
1. IOPS: Input/output operations per second
   – CURRENT IOPS: Average IOPS in 10 seconds duration
   – AVERAGE IOPS: Average of CURRENT IOPS for the complete duration of the test.
2. THROUGHPUT: Bandwidth of the data transfer – unit: MiB/s
– CURRENT THROUGHPUT and AVERAGE THROUGHPUT.
3. LATENCY: Response Time – unit: ms

This Utility considers the following Test Parameters:
1. Random and Sequential IOs (input/output operation)
2. Variable Block Sizes
3. Read/Write Ratio
4. Queue Depth
5. Active Range
6. IOs Alignment and Un-alignment
7. The entropy of the data written.

    1. Random and Sequential IOs: an IO operation is said to be random when its LBA is not related to any of parallel IOs. And if the next IO has its LBA linearly related to previous then it is sequential. The performance of Random IOs is lower than Sequential IOs. Because Sequential IOs eases up the resources management. I am using Rand() function for LBAs generation as it is repeatable.
    
    2. Variable Block Size: Block-Size is how many LBAs to consider starting from the passed LBA for the data transfer.
    
    3. Read/Write Ratio: Utility can handle any R/W ratio. It asks for Read % in the code. Reading and Writing threads are divided in that ratio but reading and writing IOs ration is not controlled - reading will continue even though write threads are stuck owing to its slowness. Thus, utility emphasis on measuring the responsiveness of the drive.
    
    4. Queue-depth: QD is defined as the number of parallel requests a drive can handle. It is controlled by controlling the Number of Threads created.
    
    5. Active Range: It is defined as the range of LBAs that must be covered under the test. While writing, low Active Range simulates the behaviour of overprovisioning in SSDs. Thus, while Writing, at low Active Range a drive might show higher performance.
    
    6. IOs Alignment and Un-Alignment: Aligning means keeping our LBAs, multiple of 8, 16 and 32 when doing Random Tests. This makes LBAs be 4K, 8K and 16K Page aligned respectively. And Alignment makes sure that while reading minimum Flash Pages are read and while writing, Alignment avoids what’s called Read-Modify-Write. Thus, when IOs are Aligned, performance improves. When IOs are un-aligned performance degrades. This utility also provides the Mix-Alignment feature in which all the LBAs are covered.
    
    7. The entropy of data: Entropy is the amount of randomness in data. Our utility lets you generate data with 10 to 100 % Entropy.

The flow of the Code:
This utility is written in C Language and the flow is very simple. It runs the selected test for a stipulated Test-Size; approximately. While running it provides Time Elapsed and Percentage of the Test Completion along with Performance parameters, after every 10 seconds (defined MACRO).

    1. Issue an Identify-namespace command and get “max_lba” (Maximum LBA a drive supports i.e. the drive size) variable initialized.
    
    2. Take all the Test Parameters like – Random or Sequential, Block Size, Read/write %, Alignment, Active Range, Start LBA in the case of the sequential IOs and the Entropy etc.
    
    3. Prepare the random data buffer as per the Entropy and Block-Size, if the test involves any Writes i.e when the Read-Percentage is not 100%.
    
    4. Lock the Mutex – I am using a mutex as threads share some global variables; the same mutex is also used to synchronise the threads for sequential IOs. Here mutex has been locked so as to block (stop) the IOs generation after the creation of threads.
    
    5. Create the threads as per the Number of Threads and Random/Sequential IOs.
    
    6. Start taking the measurement after every 10 seconds (MACRO) until the test-size completes .
    
    
    LINK to my BLOG (for more details): https://wisesciencewise.wordpress.com/2017/07/10/linux-utility-to-measure-the-performance-of-nvme-storage-drives/
