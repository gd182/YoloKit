# Keep the API callable by apps compiled against the distributed AAR.
-keep class io.github.gd182.yolokit.Backend { public protected *; }
-keep class io.github.gd182.yolokit.Detection { public protected *; }
-keep class io.github.gd182.yolokit.ModelConfig { public protected *; }
-keep class io.github.gd182.yolokit.ModelConfig$Builder { public protected *; }
-keep class io.github.gd182.yolokit.YoloKit { public protected *; }
-keep class io.github.gd182.yolokit.YoloKit$Companion { public protected *; }

# JNI exports in libyolokit.so use the original class and method names.
-include consumer-rules.pro

-keep class kotlin.Metadata { *; }
-keepattributes Signature,InnerClasses,EnclosingMethod,RuntimeVisibleAnnotations,RuntimeInvisibleAnnotations,AnnotationDefault
-repackageclasses io.github.gd182.yolokit.internal
