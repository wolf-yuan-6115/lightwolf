import { fireEvent, render, screen, within } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { SelectMenu } from "./SelectMenu";

describe("SelectMenu", () => {
  it("renders an animated auto-flipping DaisyUI listbox and selects an option", () => {
    const onChange = vi.fn();
    render(
      <SelectMenu
        value={1}
        options={[{ value: 1, label: "First" }, { value: 2, label: "Second" }]}
        onChange={onChange}
        label="Example"
      />,
    );

    const trigger = screen.getByRole("button", { name: "Example: First" });
    expect(trigger).toHaveClass("select");

    // jsdom renders popover contents but does not implement the Popover API.
    const listbox = screen.getByRole("listbox", { hidden: true });
    expect(listbox).toHaveClass("dropdown", "menu");
    expect(listbox).toHaveAttribute("aria-label", "Example");
    expect(listbox.style.positionTryFallbacks).toBe("flip-block");
    const options = within(listbox).getAllByRole("option", { hidden: true });
    expect(options[0]).toHaveAttribute("aria-selected", "true");

    fireEvent.click(options[1]);
    expect(onChange).toHaveBeenCalledWith(2);
  });
});
