; RUN: opt -passes='mygvn,verify' -S < %s | FileCheck %s

define i32 @direct(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @direct(
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[R:%.*]] = sub i32 [[X]], [[X]]
; CHECK-NEXT:    ret i32 [[R]]
  %x = add i32 %a, %b
  %y = add i32 %a, %b
  %r = sub i32 %x, %y
  ret i32 %r
}

define i32 @commutative(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @commutative(
; CHECK-NEXT:    [[X:%.*]] = mul i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[R:%.*]] = add i32 [[X]], [[X]]
; CHECK-NEXT:    ret i32 [[R]]
  %x = mul i32 %a, %b
  %y = mul i32 %b, %a
  %r = add i32 %x, %y
  ret i32 %r
}

define i32 @transitive(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: define i32 @transitive(
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[Z:%.*]] = mul i32 [[X]], [[C:%.*]]
; CHECK-NEXT:    [[R:%.*]] = sub i32 [[Z]], [[Z]]
; CHECK-NEXT:    ret i32 [[R]]
  %x = add i32 %a, %b
  %y = add i32 %b, %a
  %z = mul i32 %x, %c
  %w = mul i32 %c, %y
  %r = sub i32 %z, %w
  ret i32 %r
}

define i1 @compare(i32 %a, i32 %b) {
; CHECK-LABEL: define i1 @compare(
; CHECK-NEXT:    [[X:%.*]] = icmp eq i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[R:%.*]] = xor i1 [[X]], [[X]]
; CHECK-NEXT:    ret i1 [[R]]
  %x = icmp eq i32 %a, %b
  %y = icmp eq i32 %a, %b
  %r = xor i1 %x, %y
  ret i1 %r
}

define i64 @cast(i32 %a) {
; CHECK-LABEL: define i64 @cast(
; CHECK-NEXT:    [[X:%.*]] = sext i32 [[A:%.*]] to i64
; CHECK-NEXT:    [[R:%.*]] = add i64 [[X]], [[X]]
; CHECK-NEXT:    ret i64 [[R]]
  %x = sext i32 %a to i64
  %y = sext i32 %a to i64
  %r = add i64 %x, %y
  ret i64 %r
}

define i32 @select(i1 %c, i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @select(
; CHECK-NEXT:    [[X:%.*]] = select i1 [[C:%.*]], i32 [[A:%.*]], i32 [[B:%.*]]
; CHECK-NEXT:    [[R:%.*]] = add i32 [[X]], [[X]]
; CHECK-NEXT:    ret i32 [[R]]
  %x = select i1 %c, i32 %a, i32 %b
  %y = select i1 %c, i32 %a, i32 %b
  %r = add i32 %x, %y
  ret i32 %r
}

define ptr @gep(ptr %p, i64 %i) {
; CHECK-LABEL: define ptr @gep(
; CHECK-NEXT:    [[X:%.*]] = getelementptr i32, ptr [[P:%.*]], i64 [[I:%.*]]
; CHECK-NEXT:    ret ptr [[X]]
  %x = getelementptr i32, ptr %p, i64 %i
  %y = getelementptr i32, ptr %p, i64 %i
  ret ptr %y
}