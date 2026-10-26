; Arithmetic, stack words, a loop, a call, memory, and a system call.
; Writes "ok\n" through write(1, ...) and exits with 8 if every check
; held; a failed check exits with its own number.
;
; 算术、栈操作字、循环、调用、内存与系统调用. 每项检查都成立时经 write(1, ...)
; 写出 "ok\n" 并以 8 退出; 某项检查失败时以该检查的编号退出.
	.equ STDOUT 1
	.data counter 2
	.data buf 4
	.data checkno 2

	lit 2
	lit 3
	add
	lit 5
	call check		; 1: 2 + 3

	lit 1000
	lit 7
	mul
	lit 7000
	call check		; 2: lit16 and mul

	lit -1
	lit 4
	shr
	lit 0x0fff
	call check		; 3: shr is logical

	lit -5
	lit 3
	lt
	lit 1
	call check		; 4: lt is signed

	lit 60000
	lit 99
	lit 60000
	muldiv
	lit 99
	call check		; 5: muldiv keeps the 32-bit product

	lit 0
	lit counter
	stw
loop:	lit counter
	ldw
	lit 1
	add
	dup
	lit counter
	stw
	lit 10
	lt
	jnz loop
	lit counter
	ldw
	lit 10
	call check		; 6: loop ran ten times

	lit 'o'
	lit buf
	stb
	lit 'k'
	lit buf+1
	stb
	lit '\n'
	lit buf+2
	stb
	lit STDOUT
	lit buf
	lit 3
	sys write
	lit 3
	call check		; 7: write returned 3

	lit 8
	sys exit

; ( got want -- ) counts the checks, exits with the number of a failed one
; ( got want -- ) 计数检查项, 失败时以该项编号退出
check:	lit checkno
	ldw
	lit 1
	add
	lit checkno
	stw
	eq
	jz failed
	ret
failed:	lit checkno
	ldw
	sys exit
