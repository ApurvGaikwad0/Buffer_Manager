

.PHONY: all
all: test1 test2

test1: test_assign2_1.c buffer_mgr_stat.c storage_mgr.c dberror.c buffer_mgr.c 
	gcc -o test1 test_assign2_1.c buffer_mgr_stat.c storage_mgr.c dberror.c buffer_mgr.c

test2: test_assign2_2.c buffer_mgr_stat.c storage_mgr.c dberror.c buffer_mgr.c
	gcc -o test2 test_assign2_2.c buffer_mgr_stat.c storage_mgr.c dberror.c buffer_mgr.c


.PHONY: clean
clean:
	rm test1 test2
