; Each entry point makes one fault; the runner picks one by argc, 2 to 5.
; 每个入口点产生一种故障; 运行者按 argc (2 至 5) 选择入口.
	.data buf 2

	drop			; argv, leaving argc
	lit 2
	sub
	dup
	jz store_code
	lit 1
	sub
	dup
	jz underflow
	lit 1
	sub
	dup
	jz divide
	lit 1
	sub
	jz past_data
	lit 0
	sys exit

store_code:
	lit 1
	lit 0		; address 0 is code
	stb
	lit 0
	sys exit
underflow:
	drop
	drop
	drop
	lit 0
	sys exit
divide:
	lit 1
	lit 0
	divu
	sys exit
past_data:
	lit buf+200
	ldb
	sys exit
