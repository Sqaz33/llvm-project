; RUN: opt -passes='mygvn,verify,mygvn,verify' -S < %s | FileCheck %s

define i32 @twice(i32 %a, i32 %b, i32 %c) {
; CHECK-LABEL: define i32 @twice(
; CHECK-NEXT:    [[X:%.*]] = add i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[Y:%.*]] = mul i32 [[X]], [[C:%.*]]
; CHECK-NEXT:    [[R:%.*]] = sub i32 [[Y]], [[Y]]
; CHECK-NEXT:    ret i32 [[R]]
  %x1 = add i32 %a, %b
  %x2 = add i32 %b, %a
  %y1 = mul i32 %x1, %c
  %y2 = mul i32 %c, %x2
  %r = sub i32 %y1, %y2
  ret i32 %r
}

