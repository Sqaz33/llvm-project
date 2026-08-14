; RUN: opt -passes='mygvn,verify' -S < %s | FileCheck %s

define i32 @dominated(i1 %cond, i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @dominated(
; CHECK:       entry:
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], [[B:%.*]]
; CHECK:       left:
; CHECK-NEXT:    ret i32 [[X]]
  entry:
    %x = add i32 %a, %b
    br i1 %cond, label %left, label %right
  left:
    %y = add i32 %a, %b
    ret i32 %y
  right:
    ret i32 %x
}

define i32 @siblings(i1 %cond, i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @siblings(
; CHECK:       left:
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], [[B:%.*]]
; CHECK:       right:
; CHECK-NEXT:    [[Y:%.*]] = add i32 [[A]], [[B]]
; CHECK:       join:
; CHECK-NEXT:    [[P:%.*]] = phi i32 [ [[X]], %left ], [ [[Y]], %right ]
  entry:
    br i1 %cond, label %left, label %right
  left:
    %x = add i32 %a, %b
    br label %join
  right:
    %y = add i32 %a, %b
    br label %join
  join:
    %p = phi i32 [ %x, %left ], [ %y, %right ]
    ret i32 %p
}

define i32 @same_block(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @same_block(
; CHECK-NEXT:    [[X:%.*]] = xor i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    ret i32 [[X]]
  %x = xor i32 %a, %b
  %y = xor i32 %a, %b
  ret i32 %y
}

define i32 @unreachable_blocks(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @unreachable_blocks(
; CHECK:       dead.left:
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], [[B:%.*]]
; CHECK:       dead.right:
; CHECK-NEXT:    ret i32 [[X]]
  entry:
    ret i32 0
  dead.left:
    %x = add i32 %a, %b
    ret i32 %x
  dead.right:
    %y = add i32 %a, %b
    ret i32 %y
}

define i32 @loop_dominance(i32 %start, i32 %limit) {
; CHECK-LABEL: define i32 @loop_dominance(
; CHECK:       entry:
; CHECK-NEXT:    [[BASE:%.*]] = add i32 [[START:%.*]], 1
; CHECK:       loop:
; CHECK-NOT:     add i32 [[START]], 1
; CHECK:         [[SUM:%.*]] = add i32 [[BASE]], [[IV:%.*]]
  entry:
    %base = add i32 %start, 1
    br label %loop
  loop:
    %iv = phi i32 [ 0, %entry ], [ %iv.next, %loop ]
    %same = add i32 %start, 1
    %sum = add i32 %same, %iv
    %iv.next = add i32 %iv, 1
    %continue = icmp slt i32 %iv.next, %limit
    br i1 %continue, label %loop, label %exit
  exit:
    ret i32 %sum
}