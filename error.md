Command DWord 12 is 0 Based value in read and write command structure    
i.e to read a single block of 512B you need to pass 0 and similarly to fetch 2 512B block you will need to give 1.  
similarly for write commands;  

so in present NVMe.c it is wrong when we write   
nvme_cmd.cdw12 = block_size;

I think if we change    
**-nvme_cmd.cdw12 = block_size;**  
to   
**+nvme_cmd.cdw12 = block_size-1;**  
everywhere in NVMe.c, The code should just work fine.  
I am not changing in NVMe.c cause i cannot test and varify the changes.  

Thanks.  
