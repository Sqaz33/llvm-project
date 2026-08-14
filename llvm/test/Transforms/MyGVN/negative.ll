; RUN: opt -passes='mygvn,verify' -S < %s | FileCheck %s

define i32 @different_opcode(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @different_opcode(
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[Y:%.*]] = sub i32 [[A]], [[B]]
  %x = add i32 %a, %b
  %y = sub i32 %a, %b
  %r = xor i32 %x, %y
  ret i32 %r
}

define i32 @different_constant(i32 %a) {
; CHECK-LABEL: define i32 @different_constant(
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], 1
; CHECK-NEXT:    [[Y:%.*]] = add i32 [[A]], 2
  %x = add i32 %a, 1
  %y = add i32 %a, 2
  %r = sub i32 %x, %y
  ret i32 %r
}

define i1 @different_predicate(i32 %a, i32 %b) {
; CHECK-LABEL: define i1 @different_predicate(
; CHECK-NEXT:    [[X:%.*]] = icmp slt i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[Y:%.*]] = icmp sgt i32 [[A]], [[B]]
  %x = icmp slt i32 %a, %b
  %y = icmp sgt i32 %a, %b
  %r = xor i1 %x, %y
  ret i1 %r
}

define i32 @different_select_condition(i1 %c1, i1 %c2, i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @different_select_condition(
; CHECK-NEXT:    [[X:%.*]] = select i1 [[C1:%.*]], i32 [[A:%.*]], i32 [[B:%.*]]
; CHECK-NEXT:    [[Y:%.*]] = select i1 [[C2:%.*]], i32 [[A]], i32 [[B]]
  %x = select i1 %c1, i32 %a, i32 %b
  %y = select i1 %c2, i32 %a, i32 %b
  %r = sub i32 %x, %y
  ret i32 %r
}

define ptr @different_gep_type(ptr %p, i64 %i) {
; CHECK-LABEL: define ptr @different_gep_type(
; CHECK-NEXT:    [[X:%.*]] = getelementptr i8, ptr [[P:%.*]], i64 [[I:%.*]]
; CHECK-NEXT:    [[Y:%.*]] = getelementptr i32, ptr [[P]], i64 [[I]]
  %x = getelementptr i8, ptr %p, i64 %i
  %y = getelementptr i32, ptr %p, i64 %i
  %c = icmp eq ptr %x, %y
  %r = select i1 %c, ptr %x, ptr %y
  ret ptr %r
}

define i64 @different_integer_types(i32 %a, i64 %b) {
; CHECK-LABEL: define i64 @different_integer_types(
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], 1
; CHECK-NEXT:    [[Y:%.*]] = add i64 [[B:%.*]], 1
  %x = add i32 %a, 1
  %y = add i64 %b, 1
  %wide = zext i32 %x to i64
  %r = xor i64 %wide, %y
  ret i64 %r
}

define i32 @poison_vs_undef(i32 %a) {
; CHECK-LABEL: define i32 @poison_vs_undef(
; CHECK-NEXT:    [[FROM_POISON:%.*]] = add i32 [[A:%.*]], poison
; CHECK-NEXT:    [[FROM_UNDEF:%.*]] = add i32 [[A]], undef
; CHECK-NEXT:    [[SAFE_POISON:%.*]] = freeze i32 [[FROM_POISON]]
; CHECK-NEXT:    [[SAFE_UNDEF:%.*]] = freeze i32 [[FROM_UNDEF]]
  %from.poison = add i32 %a, poison
  %from.undef = add i32 %a, undef
  %safe.poison = freeze i32 %from.poison
  %safe.undef = freeze i32 %from.undef
  %r = xor i32 %safe.poison, %safe.undef
  ret i32 %r
}