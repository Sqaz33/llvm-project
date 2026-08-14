; RUN: opt -passes='mygvn,verify' -S < %s | FileCheck %s

define i32 @duplicate(i1 %cond) {
; CHECK-LABEL: define i32 @duplicate(
; CHECK:       join:
; CHECK-NEXT:    [[P:%.*]] = phi i32 [ 1, %left ], [ 2, %right ]
; CHECK-NEXT:    [[SUM:%.*]] = add i32 [[P]], [[P]]
  entry:
    br i1 %cond, label %left, label %right
  left:
    br label %join
  right:
    br label %join
  join:
    %a = phi i32 [ 1, %left ], [ 2, %right ]
    %b = phi i32 [ 1, %left ], [ 2, %right ]
    %sum = add i32 %a, %b
    ret i32 %sum
}

define i32 @reordered_entries(i1 %cond) {
; CHECK-LABEL: define i32 @reordered_entries(
; CHECK:       join:
; CHECK-NEXT:    [[P:%.*]] = phi i32
; CHECK-NEXT:    [[SUM:%.*]] = add i32 [[P]], [[P]]
  entry:
    br i1 %cond, label %left, label %right
  left:
    br label %join
  right:
    br label %join
  join:
    %a = phi i32 [ 10, %left ], [ 20, %right ]
    %b = phi i32 [ 20, %right ], [ 10, %left ]
    %sum = add i32 %a, %b
    ret i32 %sum
}

define i32 @different_bindings(i1 %cond) {
; CHECK-LABEL: define i32 @different_bindings(
; CHECK:       join:
; CHECK-NEXT:    [[A:%.*]] = phi i32 [ 10, %left ], [ 20, %right ]
; CHECK-NEXT:    [[B:%.*]] = phi i32 [ 10, %right ], [ 20, %left ]
; CHECK-NEXT:    [[R:%.*]] = sub i32 [[A]], [[B]]
  entry:
    br i1 %cond, label %left, label %right
  left:
    br label %join
  right:
    br label %join
  join:
    %a = phi i32 [ 10, %left ], [ 20, %right ]
    %b = phi i32 [ 10, %right ], [ 20, %left ]
    %r = sub i32 %a, %b
    ret i32 %r
}

define i32 @recursive(i1 %exit) {
; CHECK-LABEL: define i32 @recursive(
; CHECK:       loop:
; CHECK-NEXT:    [[P:%.*]] = phi i32 [ 0, %entry ], [ [[P]], %loop ]
; CHECK:       out:
; CHECK-NEXT:    [[R:%.*]] = add i32 [[P]], [[P]]
  entry:
    br label %loop
  loop:
    %a = phi i32 [ 0, %entry ], [ %a, %loop ]
    %b = phi i32 [ 0, %entry ], [ %b, %loop ]
    br i1 %exit, label %out, label %loop
  out:
    %r = add i32 %a, %b
    ret i32 %r
}

define i32 @cyclic_congruence(i32 %start, i32 %step) {
; CHECK-LABEL: define i32 @cyclic_congruence(
; CHECK:       loop:
; CHECK:         [[ACC:%.*]] = phi i32 [ 1, %entry ], [ [[NEXT:%.*]], %loop ]
; CHECK:         [[NEXT]] = add i32 [[ACC]],
; CHECK-NOT:     phi i32 [ 1, %entry ]
  entry:
    br label %loop
  loop:
    %iv = phi i32 [ %start, %entry ], [ %iv.next, %loop ]
    %a = phi i32 [ 1, %entry ], [ %a.next, %loop ]
    %b = phi i32 [ 1, %entry ], [ %b.next, %loop ]
    %iv.next = add i32 %iv, %step
    %a.next = add i32 %a, %iv.next
    %b.next = add i32 %b, %iv.next
    %keep.going = icmp sgt i32 %iv.next, 0
    br i1 %keep.going, label %loop, label %out
  out:
    %r = add i32 %iv.next, %b.next
    ret i32 %r
}

define i32 @different_parent_blocks(i1 %cond, i32 %seed) {
; CHECK-LABEL: define i32 @different_parent_blocks(
; CHECK:       left.loop:
; CHECK-NEXT:    [[LEFT:%.*]] = phi i32 [ [[SEED:%.*]], %entry ], [ [[LEFT]], %left.loop ]
; CHECK:       right.loop:
; CHECK-NEXT:    [[RIGHT:%.*]] = phi i32 [ [[SEED]], %left.exit ], [ [[RIGHT]], %right.loop ]
  entry:
    br label %left.loop
  left.loop:
    %left = phi i32 [ %seed, %entry ], [ %left, %left.loop ]
    br i1 %cond, label %left.loop, label %left.exit
  left.exit:
    br label %right.loop
  right.loop:
    %right = phi i32 [ %seed, %left.exit ], [ %right, %right.loop ]
    br i1 %cond, label %right.loop, label %exit
  exit:
    %result = add i32 %left, %right
    ret i32 %result
}

define i32 @cross_recursive(i1 %exit) {
; CHECK-LABEL: define i32 @cross_recursive(
; CHECK:       loop:
; CHECK-NEXT:    [[P:%.*]] = phi i32 [ 0, %entry ], [ [[P]], %loop ]
; CHECK:       out:
; CHECK-NEXT:    [[R:%.*]] = add i32 [[P]], [[P]]
  entry:
    br label %loop
  loop:
    %a = phi i32 [ 0, %entry ], [ %b, %loop ]
    %b = phi i32 [ 0, %entry ], [ %a, %loop ]
    br i1 %exit, label %out, label %loop
  out:
    %r = add i32 %a, %b
    ret i32 %r
}

define i32 @repeated_switch_edges(i32 %selector) {
; CHECK-LABEL: define i32 @repeated_switch_edges(
; CHECK:       join:
; CHECK-NEXT:    [[P:%.*]] = phi i32 [ 10, %entry ], [ 30, %other ], [ 10, %entry ]
; CHECK-NEXT:    [[SUM:%.*]] = add i32 [[P]], [[P]]
  entry:
    switch i32 %selector, label %other [
      i32 0, label %join
      i32 1, label %join
    ]
  other:
    br label %join
  join:
    %a = phi i32 [ 10, %entry ], [ 10, %entry ], [ 30, %other ]
    %b = phi i32 [ 10, %entry ], [ 30, %other ], [ 10, %entry ]
    %sum = add i32 %a, %b
    ret i32 %sum
}