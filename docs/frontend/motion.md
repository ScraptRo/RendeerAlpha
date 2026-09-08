# Motion

## `animate`

Milliseconds a widget eases over when the layout moves it. **Inherited by its children**,
so one number on a container animates everything under it. Zero moves at once.

```tsx
<stack id="root" animate={180}> … </stack>
```

Only what is *drawn* lags. What a widget measures as, and what its parent measures it as,
is the value the layout computed — otherwise a stack would size itself to a transient
number and the whole layout would wobble on its way to settling.

A `<list>` does not pass it down to its rows: a list puts its pooled rows in new places
every frame as it scrolls, and easing that is not a slide, it is a smear.

On a `<tabs>` it is how long a page takes to cross-fade into the next.

## `visible`

```tsx
<stack visible={() => state.expanded}> … </stack>
```

False means not drawn and not clickable. Inside a `<stack>`, and with `animate` set, a
widget becoming invisible **collapses**: the room it takes shrinks over the same duration,
so everything after it moves up rather than jumping. Elsewhere — in a scroll view, a list,
a dock panel — it still disappears at once.

---

Back to [the frontend index](README.md) · [all documentation](../README.md)
