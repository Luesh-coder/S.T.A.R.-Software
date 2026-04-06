import React, { useRef } from "react";
import { Animated, Pressable, PressableProps, StyleProp, ViewStyle } from "react-native";

const PRESSED_SCALE = 0.93;
const PRESSED_OPACITY = 0.72;
const DURATION = 110;

type Props = PressableProps & {
  style?: StyleProp<ViewStyle>;
  className?: string;
  children: React.ReactNode;
};

export function AnimatedButton({ onPress, onPressIn, onPressOut, style, className, children, ...rest }: Props) {
  const anim = useRef(new Animated.Value(0)).current;

  const handlePressIn = (e: any) => {
    Animated.timing(anim, { toValue: 1, duration: DURATION, useNativeDriver: true }).start();
    onPressIn?.(e);
  };

  const handlePressOut = (e: any) => {
    Animated.timing(anim, { toValue: 0, duration: DURATION, useNativeDriver: true }).start();
    onPressOut?.(e);
  };

  const scale = anim.interpolate({ inputRange: [0, 1], outputRange: [1, PRESSED_SCALE] });
  const opacity = anim.interpolate({ inputRange: [0, 1], outputRange: [1, PRESSED_OPACITY] });

  return (
    <Animated.View style={{ transform: [{ scale }], opacity }}>
      <Pressable
        className={className}
        style={style}
        onPress={onPress}
        onPressIn={handlePressIn}
        onPressOut={handlePressOut}
        {...rest}
      >
        {children}
      </Pressable>
    </Animated.View>
  );
}
